#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "pkg_ldconfig.h"
#include "vfs.h"
#include "vfs_stat.h"
#include "gu_dirent.h"
#include "app_printf.h"

#define LD_PATH_MAX 1400
#define CACHE_MAGIC "glibc-ld.so.cache"
#define CACHE_VERSION "1.1"
#define CACHE_HEADER_SIZE 48u
#define CACHE_ENTRY_SIZE 24u
#define CACHE_ENDIAN_LITTLE 2u
#define FLAG_ELF_LIBC6 0x0003
#define FLAG_X8664_LIB64 0x0300
#define FLAG_AMD64 (FLAG_ELF_LIBC6 | FLAG_X8664_LIB64)

typedef struct ldent {
    char *soname;
    char *path;
    int32_t flags;
} ldent_t;

typedef struct ldvec {
    ldent_t *v;
    size_t n, cap;
} ldvec_t;

typedef struct strbuf {
    unsigned char *p;
    size_t n, cap;
} strbuf_t;

static uint16_t rd16(const unsigned char *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const unsigned char *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
static void wr32(unsigned char *p, uint32_t v) { p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }
static void wr64(unsigned char *p, uint64_t v) { wr32(p,(uint32_t)v); wr32(p+4,(uint32_t)(v>>32)); }

static int sb_add(strbuf_t *b, const void *data, size_t n)
{
    size_t nc;
    unsigned char *np;
    if (b->n + n <= b->cap) { memcpy(b->p + b->n, data, n); b->n += n; return 0; }
    nc = b->cap ? b->cap : 1024;
    while (nc < b->n + n) {
        if (nc > (size_t)-1 / 2) return 1;
        nc *= 2;
    }
    np = (unsigned char *)realloc(b->p, nc);
    if (!np) return 1;
    b->p = np; b->cap = nc;
    memcpy(b->p + b->n, data, n); b->n += n;
    return 0;
}


static int ensure_parent_dirs(const char *p)
{
    char tmp[LD_PATH_MAX]; size_t i, n;
    if (!p || strlen(p) >= sizeof tmp) return 1;
    strcpy(tmp, p); n = strlen(tmp);
    for (i = 1; i < n; i++) if (tmp[i] == '/') {
        struct file *f = NULL; tmp[i] = 0;
        if (vfs_open(tmp, VFS_O_RDONLY, 0, &f) == 0) vfs_close(f);
        else if (vfs_mkdir(tmp, 0755) != 0) { tmp[i] = '/'; return 1; }
        tmp[i] = '/';
    }
    return 0;
}

static int write_file(const char *p, const void *data, size_t n)
{
    struct file *f = NULL; size_t off = 0;
    if (ensure_parent_dirs(p) != 0 || vfs_open(p, VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC, 0644, &f) != 0) return 1;
    while (off < n) {
        ssize_t w = vfs_write(f, (const unsigned char *)data + off, n - off);
        if (w <= 0) { vfs_close(f); return 1; }
        off += (size_t)w;
    }
    return vfs_close(f) != 0;
}

static int read_file(const char *p, unsigned char **out, size_t *len)
{
    struct file *f = NULL; struct g_stat st; unsigned char *buf; size_t off = 0;
    if (vfs_stat(p, &st) != 0 || st.st_size > (uint64_t)((size_t)-1) || vfs_open(p, VFS_O_RDONLY, 0, &f) != 0) return 1;
    buf = (unsigned char *)malloc((size_t)st.st_size + 1);
    if (!buf) { vfs_close(f); return 1; }
    while (off < (size_t)st.st_size) {
        ssize_t r = vfs_read(f, buf + off, (size_t)st.st_size - off);
        if (r <= 0) { vfs_close(f); free(buf); return 1; }
        off += (size_t)r;
    }
    vfs_close(f); buf[off] = 0; *out = buf; *len = off; return 0;
}

/* Parse DT_SONAME from little-endian ELF32/ELF64 ET_DYN.  Round 13's
 * supported target is Debian amd64, but accepting ELF32 here costs little. */
static int elf_soname(const unsigned char *b, size_t n, char *out, size_t cap)
{
    int cls; uint64_t phoff, dynoff = 0, dynsz = 0, strva = 0, stroff = 0, sonidx = (uint64_t)-1;
    uint16_t etype, phentsz, phnum; size_t i;
    if (n < 52 || memcmp(b, "\x7f" "ELF", 4) || b[5] != 1) return 1; /* little endian */
    cls = b[4]; etype = rd16(b + 16); if (etype != 3) return 1; /* ET_DYN */
    if (cls == 2) {
        if (n < 64) return 1;
        phoff = rd64(b + 32); phentsz = rd16(b + 54); phnum = rd16(b + 56);
    } else if (cls == 1) {
        phoff = rd32(b + 28); phentsz = rd16(b + 42); phnum = rd16(b + 44);
    } else return 1;
    if (!phentsz || phoff > n || (uint64_t)phentsz * phnum > n - phoff) return 1;
    for (i = 0; i < phnum; i++) {
        const unsigned char *p = b + (size_t)phoff + i * phentsz; uint32_t typ = rd32(p);
        if (typ == 2) {
            if (cls == 2) { dynoff = rd64(p + 8); dynsz = rd64(p + 32); }
            else { dynoff = rd32(p + 4); dynsz = rd32(p + 16); }
        }
    }
    if (!dynsz || dynoff > n || dynsz > n - dynoff) return 1;
    {
        size_t entsz = cls == 2 ? 16 : 8;
        for (i = 0; i + entsz <= dynsz; i += entsz) {
            const unsigned char *d = b + (size_t)dynoff + i;
            uint64_t tag = cls == 2 ? rd64(d) : rd32(d);
            uint64_t val = cls == 2 ? rd64(d + 8) : rd32(d + 4);
            if (tag == 0) break;
            if (tag == 5) strva = val;
            else if (tag == 14) sonidx = val;
        }
    }
    if (!strva || sonidx == (uint64_t)-1) return 1;
    for (i = 0; i < phnum; i++) {
        const unsigned char *p = b + (size_t)phoff + i * phentsz; uint32_t typ = rd32(p);
        if (typ == 1) {
            uint64_t off, va, filesz;
            if (cls == 2) { off=rd64(p+8); va=rd64(p+16); filesz=rd64(p+32); }
            else { off=rd32(p+4); va=rd32(p+8); filesz=rd32(p+16); }
            if (strva >= va && strva - va < filesz) { stroff = off + (strva - va); break; }
        }
    }
    if (!stroff || stroff > n || sonidx > n - stroff) return 1;
    {
        size_t pos = (size_t)(stroff + sonidx), j = 0;
        while (pos < n && b[pos] && j + 1 < cap) out[j++] = (char)b[pos++];
        if (pos >= n || b[pos] != 0 || j == 0) return 1;
        out[j] = 0; return 0;
    }
}

static int cache_libcmp(const char *p1, const char *p2)
{
    while (*p1) {
        if (*p1 >= '0' && *p1 <= '9') {
            if (*p2 >= '0' && *p2 <= '9') {
                unsigned long long v1 = 0, v2 = 0;
                while (*p1 >= '0' && *p1 <= '9') { v1 = v1 * 10 + (unsigned)(*p1++ - '0'); }
                while (*p2 >= '0' && *p2 <= '9') { v2 = v2 * 10 + (unsigned)(*p2++ - '0'); }
                if (v1 != v2) return v1 < v2 ? -1 : 1;
            } else return 1;
        } else if (*p2 >= '0' && *p2 <= '9') return -1;
        else if ((unsigned char)*p1 != (unsigned char)*p2) return (unsigned char)*p1 - (unsigned char)*p2;
        else { p1++; p2++; }
    }
    return (unsigned char)*p1 - (unsigned char)*p2;
}

static int ent_cmp(const void *aa, const void *bb)
{
    const ldent_t *a = (const ldent_t *)aa, *b = (const ldent_t *)bb;
    int r = cache_libcmp(b->soname, a->soname); /* glibc cache order is reversed */
    if (r) return r;
    if (a->flags < b->flags) return 1;
    if (a->flags > b->flags) return -1;
    return strcmp(a->path, b->path);
}

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1; char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int add_entry(ldvec_t *x, const char *soname, const char *path)
{
    size_t i; ldent_t *nv;
    for (i = 0; i < x->n; i++) if (!strcmp(x->v[i].soname, soname)) return 0;
    if (x->n == x->cap) {
        size_t nc = x->cap ? x->cap * 2 : 32;
        nv = (ldent_t *)realloc(x->v, nc * sizeof(*nv)); if (!nv) return 1;
        x->v = nv; x->cap = nc;
    }
    x->v[x->n].soname = dupstr(soname); x->v[x->n].path = dupstr(path); x->v[x->n].flags = FLAG_AMD64;
    if (!x->v[x->n].soname || !x->v[x->n].path) return 1;
    x->n++; return 0;
}

static int looks_like_dso(const char *n)
{
    return n && *n && strstr(n, ".so") != NULL;
}

static int scan_dir(const char *root, const char *rel, ldvec_t *ents)
{
    char full[LD_PATH_MAX]; DIR *d; struct dirent *de; unsigned added = 0;
    if (snprintf(full, sizeof full, "%s%s", root, rel) >= (int)sizeof full) return 1;
    d = vfs_opendir(full); if (!d) return 0; /* absent is fine */
    while ((de = vfs_readdir(d)) != NULL) {
        char p[LD_PATH_MAX], soname[512], cachepath[LD_PATH_MAX]; unsigned char *buf = NULL; size_t len = 0;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !looks_like_dso(de->d_name)) continue;
        if (snprintf(p, sizeof p, "%s/%s", full, de->d_name) >= (int)sizeof p) { vfs_closedir(d); return 1; }
        if (read_file(p, &buf, &len) != 0) continue;
        if (elf_soname(buf, len, soname, sizeof soname) != 0) {
            free(buf); continue;
        }
        free(buf);
        if (snprintf(cachepath, sizeof cachepath, "%s/%s", rel, de->d_name) >= (int)sizeof cachepath) { vfs_closedir(d); return 1; }
        if (add_entry(ents, soname, cachepath) != 0) { vfs_closedir(d); return 1; }
        added++;
    }
    vfs_closedir(d);
    if (added) app_printf("pkg ldconfig: scanned %s: %u candidate shared objects\n", rel, added);
    return 0;
}

static int scan_conf_file(const char *root, const char *relconf, ldvec_t *ents);

static int wildcard_match_conf(const char *name, const char *pattern)
{
    const char *star = strchr(pattern, '*');
    if (!star) return !strcmp(name, pattern);
    {
        size_t pre = (size_t)(star - pattern), suf = strlen(star + 1), nn = strlen(name);
        return nn >= pre + suf && !strncmp(name, pattern, pre) && !strcmp(name + nn - suf, star + 1);
    }
}

static int scan_include(const char *root, const char *pat, ldvec_t *ents)
{
    char dirrel[LD_PATH_MAX], namepat[512], fulldir[LD_PATH_MAX]; const char *slash = strrchr(pat, '/'); DIR *d; struct dirent *de;
    if (!slash || pat[0] != '/') return 1;
    if ((size_t)(slash - pat) >= sizeof dirrel || strlen(slash + 1) >= sizeof namepat) return 1;
    memcpy(dirrel, pat, (size_t)(slash - pat)); dirrel[slash - pat] = 0; strcpy(namepat, slash + 1);
    if (snprintf(fulldir, sizeof fulldir, "%s%s", root, dirrel) >= (int)sizeof fulldir) return 1;
    d = vfs_opendir(fulldir); if (!d) return 0;
    while ((de = vfs_readdir(d)) != NULL) if (wildcard_match_conf(de->d_name, namepat)) {
        char c[LD_PATH_MAX]; if (snprintf(c, sizeof c, "%s/%s", dirrel, de->d_name) >= (int)sizeof c) { vfs_closedir(d); return 1; }
        if (scan_conf_file(root, c, ents) != 0) { vfs_closedir(d); return 1; }
    }
    vfs_closedir(d); return 0;
}

static char *trim(char *s)
{
    char *e; while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s); while (e > s && isspace((unsigned char)e[-1])) *--e = 0; return s;
}

static int scan_conf_file(const char *root, const char *relconf, ldvec_t *ents)
{
    char p[LD_PATH_MAX]; unsigned char *buf = NULL; size_t n; char *cur;
    if (snprintf(p, sizeof p, "%s%s", root, relconf) >= (int)sizeof p) return 1;
    if (read_file(p, &buf, &n) != 0) return 0;
    cur = (char *)buf;
    while (*cur) {
        char *nl = strchr(cur, '\n'), *line, *hash;
        if (nl) *nl = 0;
        line = trim(cur); hash = strchr(line, '#'); if (hash) *hash = 0; line = trim(line);
        if (*line) {
            if (!strncmp(line, "include", 7) && isspace((unsigned char)line[7])) {
                char *pat = trim(line + 7); if (scan_include(root, pat, ents) != 0) { free(buf); return 1; }
            } else if (line[0] == '/') {
                if (scan_dir(root, line, ents) != 0) { free(buf); return 1; }
            } else {
                app_printf("pkg ldconfig: unsupported ld.so.conf line: %s\n", line); free(buf); return 1;
            }
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf); return 0;
}

static int build_cache(const char *root, ldvec_t *ents)
{
    strbuf_t strings = {0}, out = {0}; size_t i, entries_bytes, base, total; unsigned char hdr[CACHE_HEADER_SIZE] = {0}; char outpath[LD_PATH_MAX];
    uint32_t *keys = NULL, *vals = NULL;
    qsort(ents->v, ents->n, sizeof(ents->v[0]), ent_cmp);
    entries_bytes = ents->n * CACHE_ENTRY_SIZE; base = CACHE_HEADER_SIZE + entries_bytes;
    keys = (uint32_t *)calloc(ents->n ? ents->n : 1, sizeof(*keys)); vals = (uint32_t *)calloc(ents->n ? ents->n : 1, sizeof(*vals));
    if (!keys || !vals) { free(keys); free(vals); return 1; }
    for (i = 0; i < ents->n; i++) {
        size_t k = base + strings.n, l = strlen(ents->v[i].soname) + 1;
        if (k > UINT32_MAX || sb_add(&strings, ents->v[i].soname, l)) goto fail;
        keys[i] = (uint32_t)k;
        k = base + strings.n; l = strlen(ents->v[i].path) + 1;
        if (k > UINT32_MAX || sb_add(&strings, ents->v[i].path, l)) goto fail;
        vals[i] = (uint32_t)k;
    }
    if (ents->n > UINT32_MAX || strings.n > UINT32_MAX) goto fail;
    memcpy(hdr, CACHE_MAGIC, sizeof(CACHE_MAGIC)-1); memcpy(hdr + sizeof(CACHE_MAGIC)-1, CACHE_VERSION, sizeof(CACHE_VERSION)-1);
    wr32(hdr + 20, (uint32_t)ents->n); wr32(hdr + 24, (uint32_t)strings.n); hdr[28] = CACHE_ENDIAN_LITTLE;
    /* extension_offset at 32 stays zero: glibc explicitly accepts caches without extensions. */
    if (sb_add(&out, hdr, sizeof hdr)) goto fail;
    for (i = 0; i < ents->n; i++) {
        unsigned char e[CACHE_ENTRY_SIZE] = {0}; wr32(e, (uint32_t)ents->v[i].flags); wr32(e+4, keys[i]); wr32(e+8, vals[i]); wr32(e+12, 0); wr64(e+16, 0);
        if (sb_add(&out, e, sizeof e)) goto fail;
    }
    if (sb_add(&out, strings.p, strings.n)) goto fail;
    total = out.n;
    if (snprintf(outpath, sizeof outpath, "%s/etc/ld.so.cache", root) >= (int)sizeof outpath || write_file(outpath, out.p, out.n) != 0) goto fail;
    vfs_chmod(outpath, 0644);
    app_printf("pkg ldconfig: wrote /etc/ld.so.cache: %lu entries, %lu bytes\n", (unsigned long)ents->n, (unsigned long)total);
    free(keys); free(vals); free(strings.p); free(out.p); return 0;
fail:
    free(keys); free(vals); free(strings.p); free(out.p); return 1;
}

int pkg_ldconfig_run(const char *root)
{
    static const char *dirs[] = {
        "/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu",
        "/lib64", "/usr/lib64", "/lib", "/usr/lib"
    };
    ldvec_t e = {0}; size_t i;
    if (!root || !*root) return 1;
    app_printf("pkg ldconfig: native offline cache generation for %s\n", root);
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) if (scan_dir(root, dirs[i], &e) != 0) goto fail;
    if (scan_conf_file(root, "/etc/ld.so.conf", &e) != 0) goto fail;
    if (build_cache(root, &e) != 0) goto fail;
    for (i = 0; i < e.n; i++) { free(e.v[i].soname); free(e.v[i].path); }
    free(e.v); return 0;
fail:
    for (i = 0; i < e.n; i++) { free(e.v[i].soname); free(e.v[i].path); }
    free(e.v); app_printf("pkg ldconfig: FAILED; cache not considered updated\n"); return 1;
}


static int cache_string_at(const unsigned char *b, size_t n, uint32_t off, const char **out)
{
    size_t i;
    if ((size_t)off >= n) return 1;
    for (i = (size_t)off; i < n; i++) {
        if (b[i] == 0) { *out = (const char *)b + off; return 0; }
    }
    return 1;
}

int pkg_ldconfig_print_cache(const char *root)
{
    char path[LD_PATH_MAX]; unsigned char *b = NULL; size_t n = 0, i;
    uint32_t nlibs, len_strings; size_t entries_end, strings_end;
    if (!root || !*root) return 2;
    if (snprintf(path, sizeof path, "%s/etc/ld.so.cache", root) >= (int)sizeof path) return 1;
    if (read_file(path, &b, &n) != 0) {
        app_printf("pkg ldconfig-cache: cannot read /etc/ld.so.cache\n");
        return 1;
    }
    if (n < CACHE_HEADER_SIZE || memcmp(b, CACHE_MAGIC, sizeof(CACHE_MAGIC)-1) != 0 ||
        memcmp(b + sizeof(CACHE_MAGIC)-1, CACHE_VERSION, sizeof(CACHE_VERSION)-1) != 0) {
        app_printf("pkg ldconfig-cache: unsupported or invalid cache header\n"); free(b); return 1;
    }
    nlibs = rd32(b + 20); len_strings = rd32(b + 24);
    if (b[28] != CACHE_ENDIAN_LITTLE) {
        app_printf("pkg ldconfig-cache: unsupported cache byte order flag: %u\n", (unsigned)b[28]); free(b); return 1;
    }
    if ((uint64_t)CACHE_HEADER_SIZE + (uint64_t)nlibs * CACHE_ENTRY_SIZE > (uint64_t)n) {
        app_printf("pkg ldconfig-cache: corrupt entry table bounds\n"); free(b); return 1;
    }
    entries_end = CACHE_HEADER_SIZE + (size_t)nlibs * CACHE_ENTRY_SIZE;
    if (entries_end > n || (size_t)len_strings > n - entries_end) {
        app_printf("pkg ldconfig-cache: corrupt entry/string bounds\n"); free(b); return 1;
    }
    strings_end = entries_end + (size_t)len_strings;
    app_printf("Dynamic linker cache: %s\n", path);
    app_printf("Format: glibc-ld.so.cache 1.1\n");
    app_printf("Entries: %lu\n", (unsigned long)nlibs);
    for (i = 0; i < nlibs; i++) {
        const unsigned char *e = b + CACHE_HEADER_SIZE + i * CACHE_ENTRY_SIZE;
        uint32_t flags = rd32(e), key = rd32(e+4), value = rd32(e+8), osversion = rd32(e+12);
        uint64_t hwcap = rd64(e+16); const char *soname, *libpath;
        if ((size_t)key < entries_end || (size_t)key >= strings_end ||
            (size_t)value < entries_end || (size_t)value >= strings_end ||
            cache_string_at(b, strings_end, key, &soname) != 0 ||
            cache_string_at(b, strings_end, value, &libpath) != 0) {
            app_printf("pkg ldconfig-cache: corrupt entry %lu\n", (unsigned long)i); free(b); return 1;
        }
        app_printf("  %s -> %s flags=0x%04X osversion=0x%08X hwcap=0x%llX\n",
                   soname, libpath, (unsigned)flags, (unsigned)osversion, (unsigned long long)hwcap);
    }
    app_printf("Cache validation: PASS\n");
    free(b); return 0;
}
