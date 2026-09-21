#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#include "vfs_stat.h"
#include "cmds.h"
#include "deb.h"
#include "tar.h"
#include "vfs.h"
#include "logfile.h"
#include "gu_path.h"
#include "gu_xz.h"
#include "deb_control.h"
#include "deb_dep.h"
#include "app_printf.h"
#include "pkg_repo.h"
#include "pkg_state.h"
#include "pkg_plan.h"
#include "pkg_scripts.h"
#include "pkg_configure.h"
#include "pkg_preunpack.h"
#include "pkg_triggers.h"
#include "pkg_ldconfig.h"

static int raw_tar_name(const char *name, const char *stem)
{
    char want[64];
    snprintf(want, sizeof want, "%s.tar", stem);
    return !strcmp(name, want);
}

static int xz_tar_name(const char *name, const char *stem)
{
    char want[64];
    snprintf(want, sizeof want, "%s.tar.xz", stem);
    return !strcmp(name, want);
}

static FILE *open_member_tar(FILE *deb, const deb_member_t *m,
                             const char *stem,
                             uint64_t *tar_off, uint64_t *tar_len,
                             int *must_close)
{
    if (raw_tar_name(m->name, stem)) {
        *tar_off = m->offset;
        *tar_len = m->size;
        *must_close = 0;
        return deb;
    }

    if (xz_tar_name(m->name, stem)) {
        char err[128];
        FILE *tar = gu_xz_decode_member(deb, m->offset, m->size,
                                        err, sizeof err);
        if (!tar) {
            guppy_eprintf("pkg: cannot decode %s: %s\n", m->name, err);
            return NULL;
        }
        if (fseek(tar, 0, SEEK_END) != 0) {
            fclose(tar);
            return NULL;
        }
        {
            long n = ftell(tar);
            if (n < 0 || fseek(tar, 0, SEEK_SET) != 0) {
                fclose(tar);
                return NULL;
            }
            *tar_off = 0;
            *tar_len = (uint64_t)n;
        }
        *must_close = 1;
        return tar;
    }

    guppy_eprintf("pkg: unsupported %s archive compression: %s\n",
                  stem, m->name);
    return NULL;
}

static FILE *open_data_tar(FILE *deb, const deb_member_t *m,
                           uint64_t *tar_off, uint64_t *tar_len,
                           int *must_close)
{
    return open_member_tar(deb, m, "data", tar_off, tar_len, must_close);
}

static const char *clean(const char *p)
{
    while (p[0] == '.' && p[1] == '/') p += 2;
    return p;
}

static int safe_path(const char *p)
{
    const char *s = clean(p);

    if (!*s || *s == '/' || *s == '\\') return 0;

    while (*s) {
        const char *e = s;
        while (*e && *e != '/') e++;
        if ((e - s == 2 && s[0] == '.' && s[1] == '.') || e - s == 0)
            return 0;
        s = *e ? e + 1 : e;
    }
    return 1;
}

static int info(const char *path)
{
    deb_package_t p;
    gu_path_t gp;
    FILE *f = gu_path_open_seekable_read(path, &gp);
    int rc;

    if (!f) {
        guppy_eprintf("pkg: cannot open '%s'\n", path);
        return 1;
    }
    rc = deb_open_fp(f, &p);
    fclose(f);

    if (rc) {
        guppy_eprintf("pkg: invalid Debian package (rc=%d)\n", rc);
        return 1;
    }

    app_printf("Package file:    %s\n", path);
    app_printf("Source:          %s\n",
                 gp.space == GU_PATH_LOCAL ? "local" : "vfs");
    app_printf("Format:          deb %s\n", p.version);
    app_printf("Control archive: %s (%llu bytes)\n", p.control.name,
                 (unsigned long long)p.control.size);
    app_printf("Data archive:    %s (%llu bytes)\n", p.data.name,
                 (unsigned long long)p.data.size);
    return 0;
}

static int list_cb(FILE *fp, const gu_tar_entry_t *e, void *ctx)
{
    (void)fp;
    (void)ctx;

    app_printf("%c %04o %10llu %s", e->type, e->mode & 07777,
                 (unsigned long long)e->size, clean(e->path));
    if (e->type == '2') app_printf(" -> %s", e->linkname);
    app_printf("\n");
    return 0;
}

static int contents(const char *path)
{
    deb_package_t p;
    FILE *f = gu_path_open_seekable_read(path, NULL);
    FILE *tar;
    uint64_t tar_off, tar_len;
    int close_tar = 0;
    int rc;

    if (!f) {
        guppy_eprintf("pkg: cannot open '%s'\n", path);
        return 1;
    }
    rc = deb_open_fp(f, &p);
    if (rc) {
        fclose(f);
        guppy_eprintf("pkg: invalid Debian package (rc=%d)\n", rc);
        return 1;
    }

    tar = open_data_tar(f, &p.data, &tar_off, &tar_len, &close_tar);
    if (!tar) {
        fclose(f);
        return 3;
    }

    rc = gu_tar_walk(tar, tar_off, tar_len, list_cb, NULL);
    if (close_tar) fclose(tar);
    fclose(f);

    if (rc) {
        guppy_eprintf("pkg: cannot walk %s (tar rc=%d)\n", p.data.name, rc);
        return 1;
    }
    return 0;
}


typedef struct control_ctx {
    deb_control_t meta;
    int have_control;
    unsigned entries;
    unsigned scripts;
    unsigned conffiles;
} control_ctx_t;

static int is_control_path(const char *p)
{
    p = clean(p);
    return !strcmp(p, "control");
}

static int is_script_name(const char *p)
{
    p = clean(p);
    return !strcmp(p, "preinst") || !strcmp(p, "postinst") ||
           !strcmp(p, "prerm") || !strcmp(p, "postrm") ||
           !strcmp(p, "config") || !strcmp(p, "triggers");
}

static int control_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    control_ctx_t *c = vp;
    const char *rel = clean(e->path);

    if (!*rel) return 0;
    c->entries++;

    if (is_script_name(rel)) c->scripts++;
    if (!strcmp(rel, "conffiles")) c->conffiles++;

    app_printf("  %c %04o %10llu %s\n", e->type, e->mode & 07777,
                 (unsigned long long)e->size, rel);

    if (e->type == '0' && is_control_path(rel)) {
        char *buf;
        size_t n;

        if (e->size > 1024u * 1024u || e->data_offset > LONG_MAX)
            return 1;
        n = (size_t)e->size;
        buf = (char*)malloc(n + 1u);
        if (!buf) return 1;

        if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 ||
            fread(buf, 1, n, fp) != n) {
            free(buf);
            return 1;
        }
        buf[n] = 0;
        if (deb_control_parse(buf, n, &c->meta) == 0)
            c->have_control = 1;
        free(buf);
    }

    return 0;
}


static int quiet_control_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    control_ctx_t *c = vp;
    const char *rel = clean(e->path);
    if (e->type == '0' && is_control_path(rel)) {
        char *buf; size_t n;
        if (e->size > 1024u * 1024u || e->data_offset > LONG_MAX) return 1;
        n = (size_t)e->size; buf = (char*)malloc(n + 1u); if (!buf) return 1;
        if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 || fread(buf,1,n,fp) != n) { free(buf); return 1; }
        buf[n]=0; if (deb_control_parse(buf,n,&c->meta)==0) c->have_control=1; free(buf);
    }
    return 0;
}

static int load_pkg_control_metadata(const char *path, deb_control_t *meta)
{
    deb_package_t p; FILE *f=gu_path_open_seekable_read(path,NULL), *tar; uint64_t off,len; int close_tar=0,rc; control_ctx_t c;
    if(!f){ guppy_eprintf("pkg: cannot open '%s'\n",path); return 1; }
    rc=deb_open_fp(f,&p); if(rc){ fclose(f); guppy_eprintf("pkg: invalid Debian package (rc=%d)\n",rc); return 1; }
    tar=open_member_tar(f,&p.control,"control",&off,&len,&close_tar); if(!tar){ fclose(f); return 3; }
    memset(&c,0,sizeof c); rc=gu_tar_walk(tar,off,len,quiet_control_cb,&c); if(close_tar)fclose(tar); fclose(f);
    if(rc||!c.have_control){ guppy_eprintf("pkg: cannot read Debian control metadata\n"); return 1; }
    *meta=c.meta; return 0;
}

static void print_dep_field(const char *label, const char *text)
{
    deb_dep_list_t d; char err[128]; unsigned i,j;
    if(!text||!*text) return;
    app_printf("%s:\n",label);
    if(deb_dep_parse(text,&d,err,sizeof err)!=0){ app_printf("  [unparsed] %s\n",text); return; }
    for(i=0;i<d.group_count;i++){
        app_printf("  ");
        for(j=0;j<d.group[i].alt_count;j++){
            deb_dep_alt_t *a=&d.group[i].alt[j];
            if(j) app_printf(" | ");
            app_printf("%s",a->name);
            if(a->arch_qual[0]) app_printf(":%s",a->arch_qual);
            if(a->op[0]) app_printf(" (%s %s)",a->op,a->version);
            if(a->restrictions[0]) app_printf(" %s",a->restrictions);
        }
        app_printf("\n");
    }
}

static int depends_info(const char *path)
{
    deb_control_t m; int rc=load_pkg_control_metadata(path,&m); if(rc)return rc;
    app_printf("Package: %s\n",m.package); app_printf("Version: %s\n",m.version);
    if(m.architecture[0]) app_printf("Architecture: %s\n",m.architecture);
    print_dep_field("Pre-Depends",m.pre_depends);
    print_dep_field("Depends",m.depends);
    print_dep_field("Recommends",m.recommends);
    print_dep_field("Suggests",m.suggests);
    if(!m.pre_depends[0]&&!m.depends[0]&&!m.recommends[0]&&!m.suggests[0]) app_printf("Dependencies: none\n");
    return 0;
}

static void print_field(const char *label, const char *value)
{
    if (value && *value)
        app_printf("%-16s %s\n", label, value);
}

static int control_info(const char *path)
{
    deb_package_t p;
    FILE *f = gu_path_open_seekable_read(path, NULL);
    FILE *tar;
    uint64_t tar_off, tar_len;
    int close_tar = 0;
    control_ctx_t c;
    int rc;

    if (!f) {
        guppy_eprintf("pkg: cannot open '%s'\n", path);
        return 1;
    }
    rc = deb_open_fp(f, &p);
    if (rc) {
        fclose(f);
        guppy_eprintf("pkg: invalid Debian package (rc=%d)\n", rc);
        return 1;
    }

    tar = open_member_tar(f, &p.control, "control",
                          &tar_off, &tar_len, &close_tar);
    if (!tar) {
        fclose(f);
        return 3;
    }

    memset(&c, 0, sizeof c);
    app_printf("Control archive: %s (%llu bytes)\n",
                 p.control.name, (unsigned long long)p.control.size);
    app_printf("Control files:\n");

    rc = gu_tar_walk(tar, tar_off, tar_len, control_cb, &c);
    if (close_tar) fclose(tar);
    fclose(f);

    if (rc || !c.have_control) {
        guppy_eprintf("pkg: cannot read Debian control metadata\n");
        return 1;
    }

    app_printf("Metadata:\n");
    print_field("Package:", c.meta.package);
    print_field("Version:", c.meta.version);
    print_field("Architecture:", c.meta.architecture);
    print_field("Essential:", c.meta.essential);
    print_field("Priority:", c.meta.priority);
    print_field("Section:", c.meta.section);
    print_field("Maintainer:", c.meta.maintainer);
    print_field("Installed-Size:", c.meta.installed_size);
    print_field("Pre-Depends:", c.meta.pre_depends);
    print_field("Depends:", c.meta.depends);
    print_field("Recommends:", c.meta.recommends);
    print_field("Suggests:", c.meta.suggests);
    print_field("Description:", c.meta.description);
    app_printf("Control summary: %u entries, %u maintainer-script entries, %u conffiles entries\n",
                 c.entries, c.scripts, c.conffiles);
    return 0;
}

typedef struct install_ctx {
    char root[512];
    char package[128];
    unsigned files;
    unsigned dirs;
    unsigned unsupported;
    char *list_data;
    size_t list_len;
    size_t list_cap;
} install_ctx_t;

static int mkparents(const char *path)
{
    char b[1024];
    size_t i, n = strlen(path);

    if (n >= sizeof b) return -1;
    memcpy(b, path, n + 1);

    for (i = 1; i < n; i++) {
        if (b[i] == '/') {
            struct g_stat st;
            b[i] = 0;
            if (vfs_mkdir(b, 0755) != 0 && vfs_stat(b, &st) != 0) {
                b[i] = '/';
                return -1;
            }
            b[i] = '/';
        }
    }
    return 0;
}


static int vfs_write_blob(const char *path, const void *data, size_t len,
                          uint32_t mode, int append)
{
    struct file *out = NULL;
    int flags = VFS_O_WRONLY | VFS_O_CREAT |
                (append ? VFS_O_APPEND : VFS_O_TRUNC);
    const unsigned char *p = (const unsigned char*)data;
    size_t left = len;

    if (mkparents(path) != 0) return -1;
    if (vfs_open(path, flags, mode, &out) != 0) return -1;
    while (left) {
        size_t n = left > 32768u ? 32768u : left;
        if (vfs_write(out, p, n) != (ssize_t)n) {
            vfs_close(out); return -1;
        }
        p += n; left -= n;
    }
    if (vfs_close(out) != 0) return -1;
    (void)vfs_chmod(path, mode & 07777u);
    return 0;
}


static int vfs_read_text_file(const char *path, char **out, size_t *len_out)
{
    struct file *f = NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    if (!out) return -1;
    *out = NULL;
    if (len_out) *len_out = 0;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0) return -1;
    for (;;) {
        char tmp[1024];
        ssize_t n = vfs_read(f, tmp, sizeof tmp);
        if (n < 0) { vfs_close(f); free(buf); return -1; }
        if (n == 0) break;
        if (len + (size_t)n + 1u > cap) {
            size_t nc = cap ? cap : 2048u;
            char *nb;
            while (nc < len + (size_t)n + 1u) {
                if (nc > (size_t)-1 / 2u) { vfs_close(f); free(buf); return -1; }
                nc *= 2u;
            }
            nb = (char*)realloc(buf, nc);
            if (!nb) { vfs_close(f); free(buf); return -1; }
            buf = nb; cap = nc;
        }
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
    }
    vfs_close(f);
    if (!buf) {
        buf = (char*)malloc(1u);
        if (!buf) return -1;
    }
    buf[len] = 0;
    *out = buf;
    if (len_out) *len_out = len;
    return 0;
}

static int text_has_exact_line(const char *text, const char *wanted)
{
    const char *p = text;
    size_t wn = strlen(wanted);
    while (p && *p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && p[n-1] == '\r') n--;
        if (n == wn && memcmp(p, wanted, wn) == 0) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static int package_list_claims(const char *root, const char *package, const char *abspath)
{
    char path[1024], *buf = NULL;
    int rc;
    if (snprintf(path, sizeof path, "%s/var/lib/dpkg/info/%s.list", root, package) >= (int)sizeof path)
        return 0;
    if (vfs_read_text_file(path, &buf, NULL) != 0) return 0;
    rc = text_has_exact_line(buf, abspath);
    free(buf);
    return rc;
}

static int path_claimed_by_other_package(const char *root, const char *installing,
                                         const char *abspath, char *owner, size_t owner_cap)
{
    char path[1024], *status = NULL, *p;
    if (snprintf(path, sizeof path, "%s/var/lib/dpkg/status", root) >= (int)sizeof path)
        return 0;
    if (vfs_read_text_file(path, &status, NULL) != 0) return 0;
    p = status;
    while (*p) {
        const char *line_end = strchr(p, '\n');
        size_t n = line_end ? (size_t)(line_end - p) : strlen(p);
        if (n > 9u && !memcmp(p, "Package: ", 9u)) {
            char pkg[128]; size_t pn = n - 9u;
            while (pn && (p[9u + pn - 1u] == '\r' || p[9u + pn - 1u] == ' ' || p[9u + pn - 1u] == '\t')) pn--;
            if (pn >= sizeof pkg) pn = sizeof pkg - 1u;
            memcpy(pkg, p + 9u, pn); pkg[pn] = 0;
            if (pkg[0] && strcmp(pkg, installing) != 0 && package_list_claims(root, pkg, abspath)) {
                if (owner && owner_cap) { snprintf(owner, owner_cap, "%s", pkg); }
                free(status); return 1;
            }
        }
        if (!line_end) break;
        p = (char*)line_end + 1;
    }
    free(status); return 0;
}

static int path_has_diversion(const char *root, const char *abspath)
{
    char path[1024], *buf = NULL, *p;
    if (snprintf(path, sizeof path, "%s/var/lib/dpkg/diversions", root) >= (int)sizeof path)
        return 0;
    if (vfs_read_text_file(path, &buf, NULL) != 0) return 0;
    p = buf;
    while (*p) {
        char *a = p, *b, *c, *e;
        b = strchr(a, '\n'); if (!b) break; *b++ = 0;
        c = strchr(b, '\n'); if (!c) break; *c++ = 0;
        e = strchr(c, '\n'); if (e) *e = 0;
        if (!strcmp(a, abspath)) { free(buf); return 1; }
        if (!e) break;
        p = e + 1;
    }
    free(buf); return 0;
}

static int dash_bootstrap_sh_handoff(install_ctx_t *c, const char *rel,
                                     const char *dst, const char *target)
{
    char got[1024], owner[128];
    struct g_stat st;
    ssize_t rn;

    if (!c || strcmp(c->package, "dash") != 0 || strcmp(rel, "usr/bin/sh") != 0 ||
        strcmp(target, "dash") != 0)
        return 0; /* not this narrowly audited handoff */

    rn = vfs_readlink(dst, got, sizeof got - 1u);
    if (rn > 0) {
        got[rn] = 0;
        if (!strcmp(got, target)) {
            app_printf("pkg: dash handoff: /usr/bin/sh already points to dash\n");
            return 1;
        }
    }

    if (vfs_stat(dst, &st) != 0 && rn <= 0)
        return 0; /* no existing object; normal symlink creation handles it */
    if (rn <= 0 && VFS_S_ISDIR(st.st_mode)) {
        guppy_eprintf("pkg: dash handoff REFUSED: /usr/bin/sh is a directory\n");
        return -1;
    }
    if (path_has_diversion(c->root, "/usr/bin/sh") || path_has_diversion(c->root, "/bin/sh")) {
        guppy_eprintf("pkg: dash handoff REFUSED: /bin/sh or /usr/bin/sh is diverted\n");
        return -1;
    }
    owner[0] = 0;
    if (path_claimed_by_other_package(c->root, c->package, "/usr/bin/sh", owner, sizeof owner)) {
        guppy_eprintf("pkg: dash handoff REFUSED: /usr/bin/sh is owned by package %s\n", owner);
        return -1;
    }

    if (vfs_unlink(dst) != 0) {
        guppy_eprintf("pkg: dash handoff: cannot retire existing /usr/bin/sh\n");
        return -1;
    }
    if (vfs_symlink(target, dst) != 0) {
        guppy_eprintf("pkg: dash handoff: cannot create /usr/bin/sh -> dash after retiring bootstrap shell\n");
        return -1;
    }
    app_printf("pkg: dash handoff: retired unowned bootstrap /usr/bin/sh; Debian now owns /usr/bin/sh -> dash\n");
    return 1;
}

static int install_list_add(install_ctx_t *c, const char *rel)
{
    size_t n, need;
    char *nb;
    if (!c || !rel || !*rel) return 0;
    while (*rel == '/') rel++;
    n = strlen(rel);
    while (n && rel[n - 1] == '/') n--;
    if (!n) return 0;
    need = c->list_len + 1u + n + 1u;
    if (need > c->list_cap) {
        size_t cap = c->list_cap ? c->list_cap : 4096u;
        while (cap < need) {
            if (cap > (size_t)-1 / 2u) return -1;
            cap *= 2u;
        }
        nb = (char*)realloc(c->list_data, cap);
        if (!nb) return -1;
        c->list_data = nb; c->list_cap = cap;
    }
    c->list_data[c->list_len++] = '/';
    memcpy(c->list_data + c->list_len, rel, n);
    c->list_len += n;
    c->list_data[c->list_len++] = '\n';
    return 0;
}

static int load_control_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    deb_control_t *meta = (deb_control_t*)vp;
    const char *rel = clean(e->path);
    char *buf;
    size_t n;
    if (e->type != '0' || strcmp(rel, "control") != 0) return 0;
    if (e->size > 1024u * 1024u || e->data_offset > LONG_MAX) return 1;
    n = (size_t)e->size;
    buf = (char*)malloc(n + 1u);
    if (!buf) return 1;
    if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 || fread(buf, 1, n, fp) != n) {
        free(buf); return 1;
    }
    buf[n] = 0;
    if (deb_control_parse(buf, n, meta) != 0) { free(buf); return 1; }
    free(buf); return 0;
}

static int load_control_metadata(FILE *deb, const deb_member_t *m, deb_control_t *meta)
{
    FILE *tar; uint64_t off, len; int close_tar = 0; int rc;
    memset(meta, 0, sizeof *meta);
    tar = open_member_tar(deb, m, "control", &off, &len, &close_tar);
    if (!tar) return -1;
    rc = gu_tar_walk(tar, off, len, load_control_cb, meta);
    if (close_tar) fclose(tar);
    return rc == 0 && meta->package[0] ? 0 : -1;
}


typedef struct control_blob_ctx {
    const char *wanted;
    char *data;
    size_t len;
} control_blob_ctx_t;

static int control_blob_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    control_blob_ctx_t *c = (control_blob_ctx_t*)vp;
    const char *rel = clean(e->path);
    if (e->type != '0' || strcmp(rel, c->wanted) != 0) return 0;
    if (e->size > SIZE_MAX || e->data_offset > LONG_MAX) return 1;
    c->len = (size_t)e->size;
    c->data = (char*)malloc(c->len + 1u);
    if (!c->data) return 1;
    if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 ||
        (c->len && fread(c->data, 1, c->len, fp) != c->len)) {
        free(c->data); c->data = NULL; c->len = 0; return 1;
    }
    c->data[c->len] = 0;
    return 0;
}

static int load_control_blob(FILE *deb, const deb_member_t *m, const char *name,
                             char **data, size_t *len)
{
    FILE *tar; uint64_t off, tlen; int close_tar = 0, rc;
    control_blob_ctx_t c;
    memset(&c, 0, sizeof c); c.wanted = name;
    tar = open_member_tar(deb, m, "control", &off, &tlen, &close_tar);
    if (!tar) return -1;
    rc = gu_tar_walk(tar, off, tlen, control_blob_cb, &c);
    if (close_tar) fclose(tar);
    if (rc != 0) { free(c.data); return -1; }
    *data = c.data; *len = c.len;
    return 0;
}

static int control_file_print(const char *path, const char *name)
{
    deb_package_t p; FILE *f; char *buf = NULL; size_t len = 0;
    const char *q;
    if (!name || !*name || strchr(name, '/') || strchr(name, '\\')) return 2;
    for (q = name; *q; q++) if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                                  (*q >= '0' && *q <= '9') || *q == '-' || *q == '_' || *q == '.')) return 2;
    f = gu_path_open_seekable_read(path, NULL);
    if (!f) { guppy_eprintf("pkg: cannot open '%s'\n", path); return 1; }
    if (deb_open_fp(f, &p) != 0 || load_control_blob(f, &p.control, name, &buf, &len) != 0) {
        fclose(f); free(buf); return 1;
    }
    fclose(f);
    app_printf("Control file: %s [%s]\n", path, name);
    if (!buf) { app_printf("  (not present)\n"); return 0; }
    if (len) app_printf("%s", buf);
    if (!len || buf[len-1] != '\n') app_printf("\n");
    free(buf); return 0;
}

typedef struct info_copy_ctx {
    char root[512];
    char package[128];
    unsigned copied;
} info_copy_ctx_t;

static int control_info_copy_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    info_copy_ctx_t *c = (info_copy_ctx_t*)vp;
    const char *rel = clean(e->path);
    char dst[1024]; unsigned char *buf; size_t n;
    if (!*rel || e->type != '0' || !strcmp(rel, "control")) return 0;
    if (!safe_path(rel) || strchr(rel, '/')) {
        guppy_eprintf("pkg: unsupported control member path: %s\n", rel); return 1;
    }
    if (e->size > SIZE_MAX || e->data_offset > LONG_MAX) return 1;
    n = (size_t)e->size;
    buf = n ? (unsigned char*)malloc(n) : NULL;
    if (n && !buf) return 1;
    if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 || (n && fread(buf, 1, n, fp) != n)) {
        free(buf); return 1;
    }
    {
        int nw = snprintf(dst, sizeof dst, "%s/var/lib/dpkg/info/%s.%s",
                          c->root, c->package, rel);
        if (nw < 0 || (size_t)nw >= sizeof dst) { free(buf); return 1; }
    }
    if (vfs_write_blob(dst, buf ? (const void*)buf : (const void*)"", n,
                       e->mode ? (e->mode & 07777u) : 0644u, 0) != 0) {
        free(buf); return 1;
    }
    free(buf); c->copied++; return 0;
}

static int copy_control_state(FILE *deb, const deb_package_t *pkg,
                              const char *root, const char *package,
                              unsigned *copied_out)
{
    FILE *tar; uint64_t off, len; int close_tar = 0; int rc; info_copy_ctx_t c;
    memset(&c, 0, sizeof c);
    snprintf(c.root, sizeof c.root, "%s", root);
    snprintf(c.package, sizeof c.package, "%s", package);
    tar = open_member_tar(deb, &pkg->control, "control", &off, &len, &close_tar);
    if (!tar) return -1;
    rc = gu_tar_walk(tar, off, len, control_info_copy_cb, &c);
    if (close_tar) fclose(tar);
    if (copied_out) *copied_out = c.copied;
    return rc == 0 ? 0 : -1;
}

static int append_text(char **buf, size_t *len, size_t *cap, const char *text)
{
    size_t n = strlen(text); char *nb;
    if (*len + n + 1u > *cap) {
        size_t nc = *cap ? *cap : 2048u;
        while (nc < *len + n + 1u) { if (nc > (size_t)-1 / 2u) return -1; nc *= 2u; }
        nb = (char*)realloc(*buf, nc); if (!nb) return -1;
        *buf = nb; *cap = nc;
    }
    memcpy(*buf + *len, text, n); *len += n; (*buf)[*len] = 0; return 0;
}

static int status_field(char **buf, size_t *len, size_t *cap,
                        const char *name, const char *value)
{
    char line[4608];
    if (!value || !*value) return 0;
    if (snprintf(line, sizeof line, "%s: %s\n", name, value) >= (int)sizeof line) return -1;
    return append_text(buf, len, cap, line);
}

static int status_description(char **buf, size_t *len, size_t *cap, const char *value)
{
    const char *p, *nl; char line[4608]; size_t n;
    if (!value || !*value) return 0;
    p = value; nl = strchr(p, '\n'); n = nl ? (size_t)(nl - p) : strlen(p);
    if (n + 15u >= sizeof line) return -1;
    memcpy(line, "Description: ", 13); memcpy(line + 13, p, n);
    line[13+n] = '\n'; line[14+n] = 0;
    if (append_text(buf, len, cap, line) != 0) return -1;
    p = nl ? nl + 1 : NULL;
    while (p && *p) {
        nl = strchr(p, '\n'); n = nl ? (size_t)(nl - p) : strlen(p);
        if (n + 3u >= sizeof line) return -1;
        line[0] = ' '; memcpy(line+1, p, n); line[n+1]='\n'; line[n+2]=0;
        if (append_text(buf, len, cap, line) != 0) return -1;
        p = nl ? nl + 1 : NULL;
    }
    return 0;
}

static int write_status_stanza(const char *root, const deb_control_t *m)
{
    char path[1024]; char *buf = NULL; size_t len = 0, cap = 0; int rc = -1;
    if (status_field(&buf,&len,&cap,"Package",m->package)) goto out;
    if (status_field(&buf,&len,&cap,"Essential",m->essential)) goto out;
    if (append_text(&buf,&len,&cap,"Status: install ok unpacked\n")) goto out;
    if (status_field(&buf,&len,&cap,"Priority",m->priority)) goto out;
    if (status_field(&buf,&len,&cap,"Section",m->section)) goto out;
    if (status_field(&buf,&len,&cap,"Installed-Size",m->installed_size)) goto out;
    if (status_field(&buf,&len,&cap,"Maintainer",m->maintainer)) goto out;
    if (status_field(&buf,&len,&cap,"Architecture",m->architecture)) goto out;
    if (status_field(&buf,&len,&cap,"Version",m->version)) goto out;
    if (status_field(&buf,&len,&cap,"Pre-Depends",m->pre_depends)) goto out;
    if (status_field(&buf,&len,&cap,"Depends",m->depends)) goto out;
    if (status_field(&buf,&len,&cap,"Recommends",m->recommends)) goto out;
    if (status_field(&buf,&len,&cap,"Suggests",m->suggests)) goto out;
    if (status_description(&buf,&len,&cap,m->description)) goto out;
    if (append_text(&buf,&len,&cap,"\n")) goto out;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/status", root);
    rc = vfs_write_blob(path, buf, len, 0644u, 1);
out:
    free(buf); return rc;
}

static int install_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    install_ctx_t *c = vp;
    const char *rel = clean(e->path);
    char dst[1024];

    if (!*rel) return 0;
    if (!safe_path(rel)) {
        guppy_eprintf("pkg: unsafe archive path: %s\n", e->path);
        return 1;
    }

    snprintf(dst, sizeof dst, "%s/%s", c->root, rel);

    if (e->type == '5') {
        struct g_stat st;
        if (mkparents(dst) ||
            (vfs_mkdir(dst, e->mode & 07777) != 0 && vfs_stat(dst, &st) != 0))
            return 1;
        if (install_list_add(c, rel) != 0) return 1;
        c->dirs++;
        return 0;
    }

    if (e->type == '2') {
        if (!e->linkname[0]) {
            guppy_eprintf("pkg: empty symlink target: %s\n", rel);
            return 1;
        }
        if (mkparents(dst)) return 1;
        if (vfs_symlink(e->linkname, dst) != 0) {
            char got[1024];
            ssize_t rn = vfs_readlink(dst, got, sizeof got - 1u);
            if (rn > 0) {
                got[rn] = 0;
                if (!strcmp(got, e->linkname)) {
                    app_printf("pkg: link %s -> %s (already present)\n",
                               dst, e->linkname);
                    if (install_list_add(c, rel) != 0) return 1;
                    c->files++;
                    return 0;
                }
            }
            {
                int handoff = dash_bootstrap_sh_handoff(c, rel, dst, e->linkname);
                if (handoff < 0) return 1;
                if (handoff == 0) {
                    guppy_eprintf("pkg: cannot create symlink %s -> %s\n",
                                  dst, e->linkname);
                    return 1;
                }
            }
        }
        app_printf("pkg: link %s -> %s\n", dst, e->linkname);
        if (install_list_add(c, rel) != 0) return 1;
        c->files++;
        return 0;
    }

    if (e->type == '0') {
        struct file *out = NULL;
        unsigned char b[32768];
        uint64_t left = e->size;

        if (mkparents(dst)) return 1;
        if (e->data_offset > LONG_MAX || fseek(fp, (long)e->data_offset, SEEK_SET))
            return 1;
        if (vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC,
                     e->mode & 07777, &out) != 0)
            return 1;

        while (left) {
            size_t want = left < sizeof b ? (size_t)left : sizeof b;
            if (fread(b, 1, want, fp) != want ||
                vfs_write(out, b, want) != (ssize_t)want) {
                vfs_close(out);
                return 1;
            }
            left -= want;
        }
        vfs_close(out);
        vfs_chmod(dst, e->mode & 07777);
        if (install_list_add(c, rel) != 0) return 1;
        c->files++;
        return 0;
    }

    guppy_eprintf("pkg: unsupported tar entry type '%c': %s\n", e->type, rel);
    c->unsupported++;
    return 0;
}

static int install(const char *path, const char *root)
{
    deb_package_t p; deb_control_t meta;
    FILE *f = gu_path_open_seekable_read(path, NULL); FILE *tar;
    uint64_t tar_off, tar_len; int close_tar = 0; install_ctx_t c;
    char info_list[1024]; unsigned control_files = 0; int rc;
    char *preinst = NULL; size_t preinst_len = 0;
    if (!f) { guppy_eprintf("pkg: cannot open '%s'\n", path); return 1; }
    rc = deb_open_fp(f, &p);
    if (rc) { fclose(f); guppy_eprintf("pkg: invalid Debian package (rc=%d)\n", rc); return 1; }
    if (load_control_metadata(f, &p.control, &meta) != 0) {
        fclose(f); guppy_eprintf("pkg: cannot parse control metadata before install\n"); return 1;
    }
    if (load_control_blob(f, &p.control, "preinst", &preinst, &preinst_len) != 0) {
        fclose(f); guppy_eprintf("pkg: cannot inspect preinst before unpack\n"); return 1;
    }
    if (preinst) {
        rc = pkg_preunpack_fresh(meta.package, meta.version, root, preinst, preinst_len);
        free(preinst); preinst = NULL;
        if (rc != 0) {
            fclose(f);
            guppy_eprintf("pkg: pre-unpack actions failed for %s; payload was NOT unpacked\n", meta.package);
            return 1;
        }
    }
    tar = open_data_tar(f, &p.data, &tar_off, &tar_len, &close_tar);
    if (!tar) { fclose(f); return 3; }
    memset(&c, 0, sizeof c); snprintf(c.root, sizeof c.root, "%s", root);
    snprintf(c.package, sizeof c.package, "%s", meta.package);
    while (strlen(c.root) > 1 && c.root[strlen(c.root)-1] == '/') c.root[strlen(c.root)-1] = 0;
    rc = gu_tar_walk(tar, tar_off, tar_len, install_cb, &c);
    if (close_tar) fclose(tar);
    if (rc) { free(c.list_data); fclose(f); guppy_eprintf("pkg: install failed while walking %s\n", p.data.name); return 1; }
    snprintf(info_list, sizeof info_list, "%s/var/lib/dpkg/info/%s.list", c.root, meta.package);
    if (vfs_write_blob(info_list, c.list_data ? (const void*)c.list_data : (const void*)"", c.list_len, 0644u, 0) != 0) {
        free(c.list_data); fclose(f); guppy_eprintf("pkg: cannot write %s.list\n", meta.package); return 1;
    }
    free(c.list_data);
    if (copy_control_state(f, &p, c.root, meta.package, &control_files) != 0) {
        fclose(f); guppy_eprintf("pkg: cannot record control files for %s\n", meta.package); return 1;
    }
    if (pkg_triggers_register_interests(meta.package, c.root) != 0) {
        fclose(f); guppy_eprintf("pkg: cannot register trigger interests for %s\n", meta.package); return 1;
    }
    if (write_status_stanza(c.root, &meta) != 0) {
        fclose(f); guppy_eprintf("pkg: cannot update /var/lib/dpkg/status\n"); return 1;
    }
    fclose(f);
    app_printf("pkg: installed %u files, %u directories", c.files, c.dirs);
    if (c.unsupported) app_printf("; skipped %u unsupported entries", c.unsupported);
    app_printf("\n");
    app_printf("pkg: recorded %s %s as unpacked\n", meta.package, meta.version);
    app_printf("pkg: dpkg state: status + .list + %u control/info files\n", control_files);
    app_printf("pkg: maintainer scripts were recorded but NOT executed\n");
    return c.unsupported ? 4 : 0;
}

static int planned_install_cb(const char *package_path, const char *target_root, void *opaque)
{ (void)opaque; return install(package_path, target_root); }

int cmd_pkg(int argc, char **argv)
{
    if (argc >= 2) {
        if (!strcmp(argv[1], "info") && argc == 3)
            return info(argv[2]);
        if (!strcmp(argv[1], "contents") && argc == 3)
            return contents(argv[2]);
        if (!strcmp(argv[1], "control") && argc == 3)
            return control_info(argv[2]);
        if (!strcmp(argv[1], "depends") && argc == 3)
            return depends_info(argv[2]);
        if (!strcmp(argv[1], "script") && argc == 4) {
            char resolved[1400];
            const char *arg = argv[2];
            if (!strchr(arg, '/') && !strchr(arg, '\\') &&
                strncmp(arg, "local:", 6) != 0 && !strstr(arg, ".deb")) {
                if (pkg_repo_resolve_path(arg, resolved, sizeof resolved) != 0) {
                    guppy_eprintf("pkg script: package '%s' not found in registered repositories\n", arg);
                    return 1;
                }
                arg = resolved;
            }
            return pkg_script_show(arg, argv[3]);
        }
        if (!strcmp(argv[1], "control-file") && argc == 4) {
            char resolved[1400]; const char *arg = argv[2];
            if (!strchr(arg, '/') && !strchr(arg, '\\') && strncmp(arg, "local:", 6) != 0 && !strstr(arg, ".deb")) {
                if (pkg_repo_resolve_path(arg, resolved, sizeof resolved) != 0) {
                    guppy_eprintf("pkg control-file: package '%s' not found in registered repositories\n", arg); return 1;
                }
                arg = resolved;
            }
            return control_file_print(arg, argv[3]);
        }
        if (!strcmp(argv[1], "scripts") && argc == 3) {
            char resolved[1400];
            const char *arg = argv[2];
            /* A bare package name may be resolved through the registered
             * repository.  Explicit paths/local: references retain the old
             * direct-file behavior. */
            if (!strchr(arg, '/') && !strchr(arg, '\\') &&
                strncmp(arg, "local:", 6) != 0 && !strstr(arg, ".deb")) {
                if (pkg_repo_resolve_path(arg, resolved, sizeof resolved) != 0) {
                    guppy_eprintf("pkg scripts: package '%s' not found in registered repositories\n", arg);
                    return 1;
                }
                arg = resolved;
            }
            return pkg_scripts_inspect(arg);
        }
        if (!strcmp(argv[1], "repo") && argc >= 3) {
            if (!strcmp(argv[2], "add") && argc == 4) {
                char err[192] = {0};
                int n = pkg_repo_add(argv[3], err, sizeof err);
                if (n < 0) { guppy_eprintf("pkg repo add: %s\n", err[0] ? err : "failed"); return 1; }
                app_printf("pkg: repository %s: indexed %d package records\n", argv[3], n);
                return 0;
            }
            if (!strcmp(argv[2], "list") && argc == 3) { pkg_repo_list(); return 0; }
            if (!strcmp(argv[2], "delete") && argc == 4) {
                char err[192] = {0};
                if (pkg_repo_delete(argv[3], err, sizeof err) != 0) {
                    guppy_eprintf("pkg repo delete: %s\n", err[0] ? err : "failed");
                    return 1;
                }
                app_printf("pkg: repository deleted: %s\n", argv[3]);
                return 0;
            }
            if (!strcmp(argv[2], "clear") && argc == 3) { pkg_repo_clear(); app_printf("pkg: repository registry cleared\n"); return 0; }
        }
        if (!strcmp(argv[1], "lookup") && argc == 3)
            return pkg_repo_lookup_print(argv[2]);
        if (!strcmp(argv[1], "path") && argc == 3)
            return pkg_repo_path_print(argv[2]);
        if (!strcmp(argv[1], "list-installed") && argc == 3)
            return pkg_state_list(argv[2]);
        if (!strcmp(argv[1], "status") && argc == 4) {
            pkg_state_entry_t st; int sr = pkg_state_lookup(argv[2], argv[3], &st);
            if (sr != 0) { app_printf("pkg: %s is not registered under %s\n", argv[3], argv[2]); return 1; }
            app_printf("Package: %s\nVersion: %s\nStatus: %s\n", st.package, st.version, st.state); return 0;
        }
        if (!strcmp(argv[1], "plan") && argc == 4)
            return pkg_plan_run(argv[2], argv[3]);
        if (!strcmp(argv[1], "audit") && argc == 4)
            return pkg_plan_audit(argv[2], argv[3]);
        if (!strcmp(argv[1], "configure") && argc == 4)
            return pkg_configure_one(argv[2], argv[3]);
        if (!strcmp(argv[1], "triggers") && argc == 3)
            return pkg_triggers_list(argv[2]);
        if (!strcmp(argv[1], "trigger-process") && argc == 3)
            return pkg_triggers_process(argv[2]);
        if (!strcmp(argv[1], "ldconfig-cache") && argc == 3)
            return pkg_ldconfig_print_cache(argv[2]);
        if (!strcmp(argv[1], "install") && argc == 4) {
            const char *arg = argv[2];
            if (!strchr(arg, '/') && !strchr(arg, '\\') && strncmp(arg, "local:", 6) != 0 && !strstr(arg, ".deb"))
                return pkg_plan_apply(arg, argv[3], planned_install_cb, NULL);
            return install(arg, argv[3]);
        }
    }

    guppy_eprintf("usage: pkg info <package.deb>\n"
                  "       pkg contents <package.deb>\n"
                  "       pkg control <package.deb>\n"
                  "       pkg control-file <package.deb|package-name> <member> # read-only\n"
                  "       pkg depends <package.deb>\n"
                  "       pkg scripts <package.deb|package-name> # read-only scan\n"
                  "       pkg script <package.deb|package-name> <script-name> # read-only dissection\n"
                  "       pkg repo add <mounted-repository-root>\n"
                  "       pkg repo list\n"
                  "       pkg repo delete <number|root> | pkg repo clear\n"
                  "       pkg lookup <package-name>\n"
                  "       pkg path <package-name>\n"
                  "       pkg list-installed <target-root>\n"
                  "       pkg status <target-root> <package-name>\n"
                  "       pkg plan <package-name> <target-root>     # read-only\n"
                  "       pkg audit <package-name> <target-root>    # closure script census, read-only\n"
                  "       pkg configure <package-name> <target-root> # controlled offline configure\n"
                  "       pkg triggers <target-root>                  # inspect registered/pending triggers\n"
                  "       pkg trigger-process <target-root>           # process supported pending triggers\n"
                  "       pkg ldconfig-cache <target-root>            # inspect/validate offline dynamic-linker cache\n"
                  "       pkg install <package.deb|package-name> <target-root>\n"
                  "       prefix a host path with local: (example local:H:\\pkg.deb)\n");
    return 2;
}
