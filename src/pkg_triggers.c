#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pkg_triggers.h"
#include "vfs.h"
#include "app_printf.h"

#define TR_PATH_MAX 1400
#define TR_LINE_MAX 2048

typedef struct sb { char *p; size_t n, cap; } sb_t;

static int grow(sb_t *b, size_t add)
{
    size_t c; char *np;
    if (b->n + add + 1 <= b->cap) return 0;
    c = b->cap ? b->cap : 512;
    while (c < b->n + add + 1) {
        if (c > (size_t)-1 / 2) return 1;
        c *= 2;
    }
    np = (char *)realloc(b->p, c);
    if (!np) return 1;
    b->p = np; b->cap = c;
    return 0;
}

static int add(sb_t *b, const char *s, size_t n)
{
    if (grow(b, n)) return 1;
    memcpy(b->p + b->n, s, n);
    b->n += n; b->p[b->n] = 0;
    return 0;
}

static int readfile(const char *p, char **out, size_t *n)
{
    struct file *f = NULL; sb_t b = {0};
    if (vfs_open(p, VFS_O_RDONLY, 0, &f) != 0) return 1;
    for (;;) {
        char t[1024]; ssize_t r = vfs_read(f, t, sizeof t);
        if (r < 0) { vfs_close(f); free(b.p); return 1; }
        if (!r) break;
        if (add(&b, t, (size_t)r)) { vfs_close(f); free(b.p); return 1; }
    }
    vfs_close(f);
    if (!b.p) { b.p = (char *)calloc(1, 1); if (!b.p) return 1; }
    *out = b.p; if (n) *n = b.n; return 0;
}

static int parents(const char *p)
{
    char b[TR_PATH_MAX]; size_t i, n = strlen(p);
    if (n >= sizeof b) return 1;
    memcpy(b, p, n + 1);
    for (i = 1; i < n; i++) if (b[i] == '/') {
        struct file *f = NULL;
        b[i] = 0;
        if (vfs_open(b, VFS_O_RDONLY, 0, &f) == 0) vfs_close(f);
        else if (vfs_mkdir(b, 0755) != 0) { b[i] = '/'; return 1; }
        b[i] = '/';
    }
    return 0;
}

static int writefile(const char *p, const char *d, size_t n)
{
    struct file *f = NULL; size_t o = 0;
    if (parents(p) || vfs_open(p, VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC, 0644, &f) != 0) return 1;
    while (o < n) {
        ssize_t w = vfs_write(f, d + o, n - o);
        if (w <= 0) { vfs_close(f); return 1; }
        o += (size_t)w;
    }
    return vfs_close(f) != 0;
}

static int append_unique_line(const char *path, const char *line)
{
    char *old = NULL; size_t oldn = 0, ln = strlen(line), i = 0; sb_t out = {0};
    if (readfile(path, &old, &oldn) == 0) {
        while (i < oldn) {
            size_t j = i;
            while (j < oldn && old[j] != '\n') j++;
            if (j - i == ln && !memcmp(old + i, line, ln)) { free(old); return 0; }
            i = j < oldn ? j + 1 : j;
        }
        if (add(&out, old, oldn)) { free(old); return 1; }
        if (oldn && old[oldn-1] != '\n' && add(&out, "\n", 1)) { free(old); free(out.p); return 1; }
        free(old);
    }
    if (add(&out, line, ln) || add(&out, "\n", 1)) { free(out.p); return 1; }
    i = writefile(path, out.p, out.n); free(out.p); return (int)i;
}

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int valid_explicit_name(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s || !*s) return 0;
    while (*p) {
        if (isspace(*p) || *p == '/' || *p == '\\') return 0;
        p++;
    }
    return 1;
}

static int valid_file_trigger_path(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s || s[0] != '/' || !s[1]) return 0;
    while (*p) {
        if (isspace(*p) || *p == '\\') return 0;
        p++;
    }
    return 1;
}

static int register_file_interest(const char *root, const char *pkg, const char *directive, const char *name)
{
    char path[TR_PATH_MAX], line[TR_LINE_MAX];
    const char *suffix = !strcmp(directive, "interest-noawait") ? "/noawait" : "";
    if (snprintf(path, sizeof path, "%s/var/lib/dpkg/triggers/File", root) >= (int)sizeof path) return 1;
    if (snprintf(line, sizeof line, "%s %s%s", name, pkg, suffix) >= (int)sizeof line) return 1;
    return append_unique_line(path, line);
}

int pkg_trigger_activate_noawait(const char *root, const char *trigger)
{
    char path[TR_PATH_MAX], line[TR_LINE_MAX];
    if (!root || !*root || !valid_explicit_name(trigger)) return 1;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/triggers/Unincorp", root);
    snprintf(line, sizeof line, "%s -", trigger);
    if (append_unique_line(path, line) != 0) return 1;
    app_printf("pkg configure: queued no-await trigger: %s\n", trigger);
    return 0;
}

int pkg_triggers_register_interests(const char *pkg, const char *root)
{
    char path[TR_PATH_MAX], *buf = NULL, *cur; unsigned count = 0;
    if (!pkg || !*pkg || !root || !*root) return 1;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/info/%s.triggers", root, pkg);
    if (readfile(path, &buf, NULL) != 0) return 0;
    cur = buf;
    while (*cur) {
        char *nl = strchr(cur, '\n'), *line, *hash, directive[64], name[TR_LINE_MAX], interest_path[TR_PATH_MAX];
        int fields;
        if (nl) *nl = 0;
        line = trim(cur); hash = strchr(line, '#'); if (hash) *hash = 0; line = trim(line);
        if (*line) {
            fields = sscanf(line, "%63s %2047s", directive, name);
            if (fields != 2) { free(buf); return 1; }
            if (!strcmp(directive, "interest") || !strcmp(directive, "interest-await") || !strcmp(directive, "interest-noawait")) {
                if (name[0] == '/') {
                    if (!valid_file_trigger_path(name) || register_file_interest(root, pkg, directive, name) != 0) { free(buf); return 1; }
                } else {
                    if (!valid_explicit_name(name)) { free(buf); return 1; }
                    if (snprintf(interest_path, sizeof interest_path, "%s/var/lib/dpkg/triggers/%s", root, name) >= (int)sizeof interest_path) {
                        free(buf); return 1;
                    }
                    if (append_unique_line(interest_path, pkg) != 0) { free(buf); return 1; }
                }
                app_printf("pkg: trigger interest registered: %s -> %s (%s)\n", name, pkg, directive);
                count++;
            } else if (!strcmp(directive, "activate") || !strcmp(directive, "activate-await") || !strcmp(directive, "activate-noawait")) {
                /* Activation declarations are acted on during state changes, not when interests are registered. */
            } else {
                app_printf("pkg: unsupported trigger directive '%s' for %s\n", directive, pkg);
                free(buf); return 1;
            }
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf);
    if (count) app_printf("pkg: trigger interests registered: %u\n", count);
    return 0;
}

int pkg_triggers_activate_package(const char *pkg, const char *root)
{
    char path[TR_PATH_MAX], *buf = NULL, *cur; unsigned count = 0;
    if (!pkg || !*pkg || !root || !*root) return 1;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/info/%s.triggers", root, pkg);
    if (readfile(path, &buf, NULL) != 0) return 0;
    cur = buf;
    while (*cur) {
        char *nl = strchr(cur, '\n'), *line, *hash, directive[64], name[TR_LINE_MAX]; int fields;
        if (nl) *nl = 0;
        line = trim(cur); hash = strchr(line, '#'); if (hash) *hash = 0; line = trim(line);
        if (*line) {
            fields = sscanf(line, "%63s %2047s", directive, name);
            if (fields != 2) { free(buf); return 1; }
            if (!strcmp(directive, "activate-noawait")) {
                if (!valid_explicit_name(name) || pkg_trigger_activate_noawait(root, name) != 0) { free(buf); return 1; }
                count++;
            } else if (!strcmp(directive, "interest") || !strcmp(directive, "interest-await") || !strcmp(directive, "interest-noawait")) {
                if ((name[0] == '/' && !valid_file_trigger_path(name)) ||
                    (name[0] != '/' && !valid_explicit_name(name))) { free(buf); return 1; }
                /* Interest declarations do not activate anything. */
            } else if (!strcmp(directive, "activate") || !strcmp(directive, "activate-await")) {
                app_printf("pkg configure: unsupported awaiting trigger activation '%s' for %s\n", directive, pkg);
                free(buf); return 1;
            } else {
                app_printf("pkg configure: unsupported trigger directive '%s' for %s\n", directive, pkg);
                free(buf); return 1;
            }
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf);
    app_printf("pkg configure: trigger activations queued: %u\n", count);
    return 0;
}

/* ---------------- Round 13 trigger inspection / processing ---------------- */
#include "gu_dirent.h"
#include "pkg_state.h"
#include "pkg_ldconfig.h"

static int line_present(const char *path, const char *line)
{
    char *buf = NULL; size_t n = 0, ln = strlen(line), i = 0;
    if (readfile(path, &buf, &n) != 0) return 0;
    while (i < n) {
        size_t j = i; while (j < n && buf[j] != '\n') j++;
        if (j - i == ln && !memcmp(buf + i, line, ln)) { free(buf); return 1; }
        i = j < n ? j + 1 : j;
    }
    free(buf); return 0;
}

static int remove_trigger_pending(const char *root, const char *trigger)
{
    char path[TR_PATH_MAX], *buf = NULL; size_t n = 0, i = 0; sb_t out = {0};
    if (snprintf(path, sizeof path, "%s/var/lib/dpkg/triggers/Unincorp", root) >= (int)sizeof path) return 1;
    if (readfile(path, &buf, &n) != 0) return 0;
    while (i < n) {
        size_t j = i, k, namelen; char name[TR_LINE_MAX];
        while (j < n && buf[j] != '\n') j++;
        k = i; while (k < j && isspace((unsigned char)buf[k])) k++;
        namelen = 0; while (k < j && !isspace((unsigned char)buf[k]) && namelen + 1 < sizeof name) name[namelen++] = buf[k++];
        name[namelen] = 0;
        if (strcmp(name, trigger) != 0) {
            if (add(&out, buf + i, j - i) || add(&out, "\n", 1)) { free(buf); free(out.p); return 1; }
        }
        i = j < n ? j + 1 : j;
    }
    free(buf);
    if (!out.p) { out.p = (char *)calloc(1,1); if (!out.p) return 1; }
    i = writefile(path, out.p, out.n); free(out.p); return (int)i;
}

static const char *interest_directive_for(const char *root, const char *pkg, const char *trigger, char *out, size_t cap)
{
    char p[TR_PATH_MAX], *buf = NULL, *cur;
    if (snprintf(p, sizeof p, "%s/var/lib/dpkg/info/%s.triggers", root, pkg) >= (int)sizeof p) return NULL;
    if (readfile(p, &buf, NULL) != 0) return NULL;
    cur = buf;
    while (*cur) {
        char *nl = strchr(cur, '\n'), *line, *hash, d[64], n[TR_LINE_MAX];
        if (nl) *nl = 0;
        line = trim(cur); hash = strchr(line, '#'); if (hash) *hash = 0; line = trim(line);
        if (*line && sscanf(line, "%63s %2047s", d, n) == 2 && !strcmp(n, trigger) &&
            (!strcmp(d,"interest") || !strcmp(d,"interest-await") || !strcmp(d,"interest-noawait"))) {
            snprintf(out, cap, "%s", d); free(buf); return out;
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf); return NULL;
}

int pkg_triggers_list(const char *root)
{
    char dirpath[TR_PATH_MAX], unincorp[TR_PATH_MAX]; DIR *d; struct dirent *de; int any = 0;
    if (!root || !*root) return 2;
    if (snprintf(dirpath, sizeof dirpath, "%s/var/lib/dpkg/triggers", root) >= (int)sizeof dirpath) return 1;
    app_printf("Registered trigger interests:\n");
    d = vfs_opendir(dirpath);
    if (d) {
        while ((de = vfs_readdir(d)) != NULL) {
            char p[TR_PATH_MAX], *buf = NULL, *cur;
            if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..") || !strcmp(de->d_name,"Unincorp") || !strcmp(de->d_name,"Lock") || !strcmp(de->d_name,"File")) continue;
            if (snprintf(p, sizeof p, "%s/%s", dirpath, de->d_name) >= (int)sizeof p) continue;
            if (readfile(p, &buf, NULL) != 0) continue;
            cur = buf;
            while (*cur) {
                char *nl = strchr(cur,'\n'), *pkg; char directive[64] = "interest";
                if (nl) *nl = 0;
                pkg = trim(cur);
                if (*pkg) {
                    interest_directive_for(root, pkg, de->d_name, directive, sizeof directive);
                    app_printf("  %s -> %s (%s)\n", de->d_name, pkg, directive); any = 1;
                }
                if (!nl) break;
        cur = nl + 1;
            }
            free(buf);
        }
        vfs_closedir(d);
    }
    {
        char filedb[TR_PATH_MAX], *buf = NULL, *cur;
        if (snprintf(filedb, sizeof filedb, "%s/var/lib/dpkg/triggers/File", root) < (int)sizeof filedb &&
            readfile(filedb, &buf, NULL) == 0) {
            cur = buf;
            while (*cur) {
                char *nl = strchr(cur, '\n'), *line, path_name[TR_LINE_MAX], owner[TR_LINE_MAX];
                if (nl) *nl = 0;
                line = trim(cur);
                if (*line && sscanf(line, "%2047s %2047s", path_name, owner) == 2) {
                    const char *directive = "interest";
                    size_t n = strlen(owner);
                    if (n > 8 && !strcmp(owner + n - 8, "/noawait")) {
                        owner[n - 8] = 0;
                        directive = "interest-noawait";
                    }
                    app_printf("  %s -> %s (%s)\n", path_name, owner, directive);
                    any = 1;
                }
                if (!nl) break;
                cur = nl + 1;
            }
            free(buf);
        }
    }
    if (!any) app_printf("  (none)\n");
    app_printf("Pending activations:\n"); any = 0;
    if (snprintf(unincorp, sizeof unincorp, "%s/var/lib/dpkg/triggers/Unincorp", root) < (int)sizeof unincorp) {
        char *buf = NULL, *cur;
        if (readfile(unincorp, &buf, NULL) == 0) {
            cur = buf;
            while (*cur) {
                char *nl = strchr(cur,'\n'), *line;
                if (nl) *nl = 0;
                line = trim(cur);
                if (*line) { app_printf("  %s\n", line); any = 1; }
                if (!nl) break;
        cur = nl + 1;
            }
            free(buf);
        }
    }
    if (!any) app_printf("  (none)\n");
    return 0;
}

static int process_ldconfig_trigger(const char *root)
{
    char interest[TR_PATH_MAX]; pkg_state_entry_t st; const char *last;
    if (snprintf(interest, sizeof interest, "%s/var/lib/dpkg/triggers/ldconfig", root) >= (int)sizeof interest) return 1;
    if (!line_present(interest, "libc-bin")) {
        app_printf("pkg triggers: ldconfig pending but no registered libc-bin interest; left pending\n"); return 1;
    }
    if (pkg_state_lookup(root, "libc-bin", &st) != 0) {
        app_printf("pkg triggers: ldconfig consumer libc-bin is not registered; left pending\n"); return 1;
    }
    last = strrchr(st.state, ' '); last = (last && last[1]) ? last + 1 : st.state;
    if (strcmp(last, "installed") != 0) {
        app_printf("pkg triggers: ldconfig consumer libc-bin is %s; configure it first; left pending\n", last); return 1;
    }
    app_printf("pkg triggers: dispatch ldconfig -> libc-bin (triggered)\n");
    if (pkg_ldconfig_run(root) != 0) { app_printf("pkg triggers: libc-bin triggered ldconfig failed; left pending\n"); return 1; }
    if (remove_trigger_pending(root, "ldconfig") != 0) return 1;
    app_printf("pkg triggers: completed and retired pending trigger: ldconfig\n"); return 0;
}

int pkg_triggers_process(const char *root)
{
    char p[TR_PATH_MAX], *buf = NULL, *cur; int failures = 0, processed = 0;
    if (!root || !*root) return 2;
    if (snprintf(p, sizeof p, "%s/var/lib/dpkg/triggers/Unincorp", root) >= (int)sizeof p) return 1;
    if (readfile(p, &buf, NULL) != 0) { app_printf("pkg triggers: no pending activations\n"); return 0; }
    cur = buf;
    while (*cur) {
        char *nl = strchr(cur,'\n'), *line, name[TR_LINE_MAX]; size_t k = 0;
        if (nl) *nl = 0;
        line = trim(cur);
        while (*line && !isspace((unsigned char)*line) && k + 1 < sizeof name) name[k++] = *line++;
        name[k] = 0;
        if (*name) {
            if (!strcmp(name, "ldconfig")) { if (process_ldconfig_trigger(root) == 0) processed++; else failures++; }
            else app_printf("pkg triggers: unsupported pending trigger '%s'; left pending\n", name);
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf);
    app_printf("pkg triggers: processing complete: %d processed, %d failed; unsupported triggers preserved\n", processed, failures);
    return failures ? 1 : 0;
}
