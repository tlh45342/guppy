#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "pkg_configure.h"
#include "pkg_state.h"
#include "vfs.h"
#include "app_printf.h"
#include "pkg_triggers.h"
#include "pkg_ldconfig.h"
#include "vfs_stat.h"

#define CFG_PATH_MAX 1400
#define CFG_LINE_MAX 2048

typedef struct strbuf {
    char *p;
    size_t n;
    size_t cap;
} strbuf_t;

static int sb_grow(strbuf_t *b, size_t need)
{
    size_t nc;
    char *np;
    if (b->n + need + 1 <= b->cap) return 0;
    nc = b->cap ? b->cap : 4096;
    while (nc < b->n + need + 1) {
        if (nc > (size_t)-1 / 2) return -1;
        nc *= 2;
    }
    np = (char *)realloc(b->p, nc);
    if (!np) return -1;
    b->p = np; b->cap = nc;
    return 0;
}

static int sb_add(strbuf_t *b, const char *s, size_t n)
{
    if (sb_grow(b, n) != 0) return -1;
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
    return 0;
}

static int read_vfs_file(const char *path, char **out, size_t *len)
{
    struct file *f = NULL;
    strbuf_t b;
    memset(&b, 0, sizeof b);
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0) return -1;
    for (;;) {
        char tmp[4096];
        ssize_t n = vfs_read(f, tmp, sizeof tmp);
        if (n < 0) { vfs_close(f); free(b.p); return -1; }
        if (n == 0) break;
        if (sb_add(&b, tmp, (size_t)n) != 0) { vfs_close(f); free(b.p); return -1; }
    }
    vfs_close(f);
    if (!b.p) {
        b.p = (char *)malloc(1);
        if (!b.p) return -1;
        b.p[0] = 0;
    }
    *out = b.p;
    if (len) *len = b.n;
    return 0;
}

static int path_exists(const char *path)
{
    struct file *f = NULL;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0) return 0;
    vfs_close(f);
    return 1;
}

static int ensure_dir(const char *path, unsigned mode)
{
    char tmp[CFG_PATH_MAX];
    size_t i, n;
    if (!path || !*path || strlen(path) >= sizeof tmp) return -1;
    strcpy(tmp, path);
    n = strlen(tmp);
    while (n > 1 && tmp[n-1] == '/') tmp[--n] = 0;
    for (i = 1; i <= n; i++) {
        if (tmp[i] != '/' && tmp[i] != 0) continue;
        {
            char saved = tmp[i];
            tmp[i] = 0;
            if (*tmp && !path_exists(tmp)) {
                if (vfs_mkdir(tmp, mode) != 0) { tmp[i] = saved; return -1; }
            }
            tmp[i] = saved;
        }
    }
    return 0;
}

static int valid_target_rel(const char *p)
{
    const char *s;
    if (!p || p[0] != '/') return 0;
    for (s = p; *s; s++) {
        if (s[0] == '.' && s[1] == '.' &&
            (s == p || s[-1] == '/') && (s[2] == 0 || s[2] == '/')) return 0;
    }
    return 1;
}

static int parse_mode(const char *s, unsigned *mode)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !*s || !strcmp(s, "-")) { *mode = 0755; return 0; }
    errno = 0;
    v = strtoul(s, &end, 8);
    if (errno || !end || *end || v > 07777u) return -1;
    *mode = (unsigned)v;
    return 0;
}

static int parse_id(const char *s, uint32_t *id)
{
    char *end = NULL;
    unsigned long v;
    if (!strcmp(s, "root")) { *id = 0; return 0; }
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno || !end || *end || v > 0xffffffffUL) return -1;
    *id = (uint32_t)v;
    return 0;
}

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int tmpfiles_apply(const char *root, const char *conf)
{
    char path[CFG_PATH_MAX], *buf = NULL, *cur;
    unsigned applied = 0;
    snprintf(path, sizeof path, "%s/usr/lib/tmpfiles.d/%s", root, conf);
    if (read_vfs_file(path, &buf, NULL) != 0) {
        app_printf("pkg configure: tmpfiles recipe not found: %s\n", path);
        return 1;
    }
    app_printf("pkg configure: tmpfiles --create %s\n", conf);
    cur = buf;
    while (*cur) {
        char *nl = strchr(cur, '\n');
        char *line, *hash;
        char type[32], rel[CFG_PATH_MAX], modes[32], uid_s[64], gid_s[64], age[64], arg[256];
        int fields;
        unsigned mode;
        uint32_t uid, gid;
        char dst[CFG_PATH_MAX];
        if (nl) *nl = 0;
        line = trim(cur);
        hash = strchr(line, '#');
        if (hash) *hash = 0;
        line = trim(line);
        if (*line) {
            arg[0] = 0;
            fields = sscanf(line, "%31s %1399s %31s %63s %63s %63s %255[^\n]",
                            type, rel, modes, uid_s, gid_s, age, arg);
            if (fields < 6) {
                app_printf("pkg configure: unsupported tmpfiles line: %s\n", line);
                free(buf); return 1;
            }
            if (strcmp(type, "d") != 0) {
                app_printf("pkg configure: unsupported tmpfiles type '%s' in %s\n", type, conf);
                free(buf); return 1;
            }
            if (!valid_target_rel(rel) || parse_mode(modes, &mode) != 0 ||
                parse_id(uid_s, &uid) != 0 || parse_id(gid_s, &gid) != 0) {
                app_printf("pkg configure: invalid tmpfiles directory entry in %s: %s\n", conf, line);
                free(buf); return 1;
            }
            if (snprintf(dst, sizeof dst, "%s%s", root, rel) >= (int)sizeof dst) {
                free(buf); return 1;
            }
            if (ensure_dir(dst, mode) != 0 || vfs_chmod(dst, mode) != 0 || vfs_chown(dst, uid, gid) != 0) {
                app_printf("pkg configure: failed tmpfiles directory %s\n", dst);
                free(buf); return 1;
            }
            app_printf("pkg configure: tmpfiles d %s mode=%04o uid=%lu gid=%lu\n",
                       rel, mode, (unsigned long)uid, (unsigned long)gid);
            applied++;
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf);
    app_printf("pkg configure: tmpfiles entries applied: %u\n", applied);
    return 0;
}

static int write_vfs_file(const char *path, const char *data, size_t len, unsigned mode)
{
    struct file *f = NULL;
    size_t off = 0;
    char parent[CFG_PATH_MAX], *slash;
    if (strlen(path) >= sizeof parent) return -1;
    strcpy(parent, path); slash = strrchr(parent, '/');
    if (slash && slash != parent) { *slash = 0; if (ensure_dir(parent, 0755) != 0) return -1; }
    if (vfs_open(path, VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC, mode, &f) != 0) return -1;
    while (off < len) { ssize_t n=vfs_write(f,data+off,len-off); if(n<=0){vfs_close(f);return -1;} off+=(size_t)n; }
    return vfs_close(f)==0 ? 0 : -1;
}

static int touch_vfs_file(const char *path, unsigned mode)
{
    if (path_exists(path)) return 0;
    /* EXT2 creation is committed by the first write in the current VFS. A single
     * blank line is semantically empty for these PAM text state files. */
    return write_vfs_file(path, "\n", 1, mode);
}

static int replace_token(char **buf, const char *token, const char *value)
{
    char *p=*buf,*at; strbuf_t out={0}; size_t tl=strlen(token),vl=strlen(value);
    while ((at=strstr(p,token))!=NULL) {
        if(sb_add(&out,p,(size_t)(at-p))||sb_add(&out,value,vl)){free(out.p);return -1;}
        p=at+tl;
    }
    if(sb_add(&out,p,strlen(p))){free(out.p);return -1;}
    free(*buf); *buf=out.p; return 0;
}

static int pam_generate_one(const char *root,const char *name,const char *primary,const char *additional)
{
    char src[CFG_PATH_MAX],dst[CFG_PATH_MAX]; char *buf=NULL;
    char tok1[96],tok2[96];
    snprintf(src,sizeof src,"%s/usr/share/pam/%s",root,name);
    snprintf(dst,sizeof dst,"%s/etc/pam.d/%s",root,name);
    if(read_vfs_file(src,&buf,NULL)!=0)return 1;
    if(!strcmp(name,"common-auth")){strcpy(tok1,"$auth_primary");strcpy(tok2,"$auth_additional");}
    else if(!strcmp(name,"common-account")){strcpy(tok1,"$account_primary");strcpy(tok2,"$account_additional");}
    else if(!strcmp(name,"common-password")){strcpy(tok1,"$password_primary");strcpy(tok2,"$password_additional");}
    else if(!strcmp(name,"common-session")){strcpy(tok1,"$session_primary");strcpy(tok2,"$session_additional");}
    else {strcpy(tok1,"$session_nonint_primary");strcpy(tok2,"$session_nonint_additional");}
    if(replace_token(&buf,tok1,primary)||replace_token(&buf,tok2,additional)||
       write_vfs_file(dst,buf,strlen(buf),0644)!=0){free(buf);return 1;}
    free(buf); app_printf("pkg configure: PAM generated /etc/pam.d/%s (Unix authentication)\n",name); return 0;
}

static int configure_libpam_runtime(const char *root)
{
    char profile[CFG_PATH_MAX],*buf=NULL;
    snprintf(profile,sizeof profile,"%s/usr/share/pam-configs/unix",root);
    if(read_vfs_file(profile,&buf,NULL)!=0 || !strstr(buf,"Name: Unix authentication") ||
       !strstr(buf,"Default: yes") || !strstr(buf,"pam_unix.so")) {
        free(buf); app_printf("pkg configure: unsupported/missing PAM Unix profile\n"); return 1;
    }
    free(buf);
    if(pam_generate_one(root,"common-auth","auth\t[success=1 default=ignore]\tpam_unix.so nullok","") ||
       pam_generate_one(root,"common-account","account\t[success=1 new_authtok_reqd=done default=ignore]\tpam_unix.so ","") ||
       pam_generate_one(root,"common-password","password\t[success=1 default=ignore]\tpam_unix.so obscure yescrypt","") ||
       pam_generate_one(root,"common-session","session\t[default=1]\t\t\tpam_permit.so","session\trequired\tpam_unix.so ") ||
       pam_generate_one(root,"common-session-noninteractive","session\t[default=1]\t\t\tpam_permit.so","session\trequired\tpam_unix.so ")) return 1;
    app_printf("pkg configure: libpam-runtime default Unix PAM profile materialized offline\n"); return 0;
}

static int configure_libpam_modules(const char *root)
{
    char p[CFG_PATH_MAX];
    snprintf(p,sizeof p,"%s/etc/security/opasswd",root); if(touch_vfs_file(p,0600)!=0)return 1;
    if(vfs_chmod(p,0600)!=0)return 1;
    snprintf(p,sizeof p,"%s/etc/environment",root); if(touch_vfs_file(p,0644)!=0)return 1;
    app_printf("pkg configure: libpam-modules created fresh-install PAM state files\n"); return 0;
}

static int known_noop_postinst(const char *pkg,const char *root)
{
    char path[CFG_PATH_MAX],*b=NULL; int ok=0;
    snprintf(path,sizeof path,"%s/var/lib/dpkg/info/%s.postinst",root,pkg);
    if(read_vfs_file(path,&b,NULL)!=0)return 0;
    if(!strcmp(pkg,"login")) ok=strstr(b,"rm_conffile /etc/securetty")&&strstr(b,"rm_conffile /etc/login.defs");
    else if(!strcmp(pkg,"login.defs")) ok=strstr(b,"_adopt_conffile")&&strstr(b,"${conffile}.dpkg-bak");
    else if(!strcmp(pkg,"libpam0g")) ok=strstr(b,"if [ ! -z \"$2\" ]; then")&&strstr(b,"Restarting services possibly affected by the upgrade");
    else if(!strcmp(pkg,"libpam-modules-bin")) ok=strstr(b,"pam_namespace.service")&&strstr(b,"if [ -d /run/systemd/system ]; then");
    free(b); if(ok)app_printf("pkg configure: %s fresh-install postinst audited as offline no-op\n",pkg); return ok?1:-1;
}

static int configure_util_linux(const char *root)
{
    char post[CFG_PATH_MAX],*b=NULL,pager[CFG_PATH_MAX],man[CFG_PATH_MAX];
    snprintf(post,sizeof post,"%s/var/lib/dpkg/info/util-linux.postinst",root);
    if(read_vfs_file(post,&b,NULL)!=0 || !strstr(b,"update-alternatives --install /usr/bin/pager pager /bin/more 50") ||
       !strstr(b,"fstrim.timer") || !strstr(b,"fstrim.service")){free(b);return 1;} free(b);
    snprintf(pager,sizeof pager,"%s/usr/bin/pager",root);
    if(!path_exists(pager) && vfs_symlink("/bin/more",pager)!=0)return 1;
    snprintf(man,sizeof man,"%s/usr/share/man/man1/pager.1.gz",root);
    if(!path_exists(man) && vfs_symlink("more.1.gz",man)!=0)return 1;
    app_printf("pkg configure: util-linux installed pager alternative; offline systemd fstrim actions skipped\n"); return 0;
}

static int verified_noop_preinst(const char *pkg,const char *root)
{
    char path[CFG_PATH_MAX],*b=NULL; int ok=0;
    snprintf(path,sizeof path,"%s/var/lib/dpkg/info/%s.preinst",root,pkg);
    if(read_vfs_file(path,&b,NULL)!=0)return 0;
    if(!strcmp(pkg,"login")) ok=strstr(b,"rm_conffile /etc/securetty")&&strstr(b,"rm_conffile /etc/login.defs");
    else if(!strcmp(pkg,"libpam-modules")) ok=strstr(b,"handle_profiles_with_removed_modules()")&&strstr(b,"lt-nl 1.4.0-5");
    free(b); return ok;
}

static int extract_tmpfiles_name(const char *line, char *out, size_t cap)
{
    const char *p = strstr(line, "--create");
    size_t n = 0;
    if (!p) return -1;
    p += 8;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\"' || *p == '\'') p++;
    while (*p && !isspace((unsigned char)*p) && *p != '\"' && *p != '\'' && *p != ';' && *p != '|') {
        if (n + 1 >= cap) return -1;
        out[n++] = *p++;
    }
    out[n] = 0;
    return n ? 0 : -1;
}

/* Recognize the fresh-install portion of the debhelper tmpfiles postinst
 * template.  We intentionally do not interpret shell in general. */
static int process_postinst_fresh(const char *pkg, const char *root)
{
    char path[CFG_PATH_MAX], *buf = NULL, *cur;
    int upgrade_guard_depth = 0;
    int tmpfiles_seen = 0;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/info/%s.postinst", root, pkg);
    if (read_vfs_file(path, &buf, NULL) != 0) return 0;

    cur = buf;
    while (*cur) {
        char *nl = strchr(cur, '\n');
        char *line;
        if (nl) *nl = 0;
        line = trim(cur);
        if (!*line || *line == '#' || !strncmp(line, "#!", 2) || !strcmp(line, "set -e")) {
            /* structural/no-op */
        } else if (strstr(line, "dpkg --compare-versions \"$2\"") && strstr(line, "if [ \"$1\" = configure ]")) {
            upgrade_guard_depth++;
            app_printf("pkg configure: skipped previous-version-only branch (fresh install)\n");
        } else if (upgrade_guard_depth) {
            if (!strcmp(line, "fi")) upgrade_guard_depth--;
            /* The whole guarded block is intentionally skipped for fresh install. */
        } else if (strstr(line, "if [ \"$1\" = \"configure\" ]") == line && strstr(line, "abort-")) {
            /* outer debhelper action guard */
        } else if (strstr(line, "if [ -x \"$(command -v systemd-tmpfiles)\" ]; then")) {
            /* helper availability guard; Guppy supplies the supported primitive */
        } else if (strstr(line, "systemd-tmpfiles") && strstr(line, "--create")) {
            char conf[256];
            if (extract_tmpfiles_name(line, conf, sizeof conf) != 0 || tmpfiles_apply(root, conf) != 0) {
                free(buf); return 1;
            }
            tmpfiles_seen++;
        } else if (!strcmp(line, "fi")) {
            /* closes a supported debhelper guard */
        } else if (!strncmp(line, "dpkg-trigger --no-await ", 24)) {
            char *name = trim(line + 24);
            char *end = name;
            while (*end && !isspace((unsigned char)*end) && *end != ';' && *end != '|') end++;
            *end = 0;
            if (pkg_trigger_activate_noawait(root, name) != 0) { free(buf); return 1; }
        } else {
            app_printf("pkg configure: unsupported postinst line for %s: %s\n", pkg, line);
            free(buf); return 1;
        }
        if (!nl) break;
        cur = nl + 1;
    }
    free(buf);
    if (upgrade_guard_depth) {
        app_printf("pkg configure: malformed previous-version guard in %s.postinst\n", pkg);
        return 1;
    }
    app_printf("pkg configure: supported postinst actions applied: %d\n", tmpfiles_seen);
    return 0;
}


static int diversions_contain_record(const char *root, const char *from, const char *to, const char *owner)
{
    char path[CFG_PATH_MAX], rec[CFG_PATH_MAX * 2 + 256], *buf = NULL;
    int n;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/diversions", root);
    if (read_vfs_file(path, &buf, NULL) != 0) return 0;
    n = snprintf(rec, sizeof rec, "%s\n%s\n%s\n", from, to, owner);
    if (n < 0 || (size_t)n >= sizeof rec) { free(buf); return 0; }
    n = strstr(buf, rec) != NULL;
    free(buf);
    return n;
}

static int libc6_preunpack_attested(const char *root)
{
    return diversions_contain_record(root,
            "/lib64", "/.lib64.usr-is-merged", "base-files") &&
           diversions_contain_record(root,
            "/lib64/ld-linux-x86-64.so.2",
            "/lib64/ld-linux-x86-64.so.2.usr-is-merged", "libc6");
}

/* Controlled fresh-install interpretation of the Debian 13 libc6 postinst.
 * The full script was audited in Round 8.  Under DPKG_ROOT and with no
 * previous version, the live-system/upgrade branches do not apply.  The
 * fresh configuration action that remains is the no-await libc-upgrade
 * trigger.  We still require script signatures so a changed postinst fails
 * closed instead of being silently treated as equivalent. */
static int process_libc6_postinst_fresh(const char *root)
{
    char path[CFG_PATH_MAX], *buf = NULL;
    snprintf(path, sizeof path, "%s/var/lib/dpkg/info/libc6.postinst", root);
    if (read_vfs_file(path, &buf, NULL) != 0) {
        app_printf("pkg configure: libc6 postinst not found\n");
        return 1;
    }
    if (!strstr(buf, "if [ \"$type\" = configure ]") ||
        !strstr(buf, "dpkg-trigger --no-await libc-upgrade") ||
        !strstr(buf, "DPKG_ROOT") ||
        !strstr(buf, "libc-major-upgrade")) {
        app_printf("pkg configure: unsupported libc6 postinst signature; package remains unpacked\n");
        free(buf);
        return 1;
    }
    free(buf);
    app_printf("pkg configure: libc6 fresh-install postinst: upgrade/live-system branches skipped\n");
    if (pkg_trigger_activate_noawait(root, "libc-upgrade") != 0) return 1;
    app_printf("pkg configure: libc6 fresh-install postinst actions applied: 1\n");
    return 0;
}


static int copy_file_preserve_if_missing(const char *src, const char *dst)
{
    struct g_stat st;
    struct file *in = NULL, *out = NULL;
    char parent[CFG_PATH_MAX];
    size_t i, n;
    if (path_exists(dst)) return 0;
    if (vfs_stat(src, &st) != 0 || vfs_open(src, VFS_O_RDONLY, 0, &in) != 0) return 1;
    if (strlen(dst) >= sizeof parent) { vfs_close(in); return 1; }
    strcpy(parent, dst); n = strlen(parent);
    for (i = n; i > 0; i--) if (parent[i-1] == '/') { parent[i-1] = 0; break; }
    if (*parent && ensure_dir(parent, 0755) != 0) { vfs_close(in); return 1; }
    if (vfs_open(dst, VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC, st.st_mode & 07777u, &out) != 0) { vfs_close(in); return 1; }
    for (;;) {
        char buf[4096]; ssize_t r = vfs_read(in, buf, sizeof buf); size_t off = 0;
        if (r < 0) { vfs_close(in); vfs_close(out); return 1; }
        if (r == 0) break;
        while (off < (size_t)r) {
            ssize_t w = vfs_write(out, buf + off, (size_t)r - off);
            if (w <= 0) { vfs_close(in); vfs_close(out); return 1; }
            off += (size_t)w;
        }
    }
    vfs_close(in);
    if (vfs_close(out) != 0 || vfs_chmod(dst, st.st_mode & 07777u) != 0 || vfs_chown(dst, st.st_uid, st.st_gid) != 0) return 1;
    return 0;
}

static int process_libc_bin_postinst_fresh(const char *root)
{
    char post[CFG_PATH_MAX], src[CFG_PATH_MAX], dst[CFG_PATH_MAX];
    char *buf = NULL;
    snprintf(post, sizeof post, "%s/var/lib/dpkg/info/libc-bin.postinst", root);
    if (read_vfs_file(post, &buf, NULL) != 0) {
        app_printf("pkg configure: libc-bin postinst not found\n");
        return 1;
    }
    if (!strstr(buf, "install_from_default") ||
        !strstr(buf, "usr/share/libc-bin/nsswitch.conf") ||
        !strstr(buf, "etc/nsswitch.conf") ||
        !strstr(buf, "ldconfig -r \"$DPKG_ROOT/\"") ||
        !strstr(buf, "[ \"$1\" = \"triggered\" ]") ||
        !strstr(buf, "[ \"$1\" = \"configure\" ]")) {
        app_printf("pkg configure: unsupported libc-bin postinst signature; package remains unpacked\n");
        free(buf); return 1;
    }
    free(buf);
    snprintf(src, sizeof src, "%s/usr/share/libc-bin/nsswitch.conf", root);
    snprintf(dst, sizeof dst, "%s/etc/nsswitch.conf", root);
    if (!path_exists(dst)) {
        if (copy_file_preserve_if_missing(src, dst) != 0) {
            app_printf("pkg configure: failed libc-bin default nsswitch.conf install\n");
            return 1;
        }
        app_printf("pkg configure: libc-bin installed default /etc/nsswitch.conf\n");
    } else {
        app_printf("pkg configure: libc-bin preserved existing /etc/nsswitch.conf\n");
    }
    if (pkg_ldconfig_run(root) != 0) return 1;
    app_printf("pkg configure: libc-bin fresh-install postinst actions applied: 2\n");
    return 0;
}

int pkg_state_set_installed(const char *root, const char *package);

int pkg_configure_one(const char *package, const char *target_root)
{
    pkg_state_entry_t st;
    char preinst[CFG_PATH_MAX];
    const char *last;
    if (!package || !*package || !target_root || !*target_root) return 2;
    if (pkg_state_lookup(target_root, package, &st) != 0) {
        app_printf("pkg configure: %s is not registered under %s\n", package, target_root);
        return 1;
    }
    last = strrchr(st.state, ' ');
    last = (last && last[1]) ? last + 1 : st.state;
    if (!strcmp(last, "installed")) {
        app_printf("pkg configure: %s %s is already installed\n", st.package, st.version);
        return 0;
    }
    if (strcmp(last, "unpacked") != 0) {
        app_printf("pkg configure: unsupported package state for %s: %s\n", package, st.state);
        return 1;
    }

    app_printf("Package configure: %s %s\n", st.package, st.version);
    app_printf("Target root: %s\n", target_root);
    app_printf("Mode: controlled offline configuration (fresh install)\n");
    app_printf("Shell execution: DISABLED\n");

    /* A preinst belongs before unpack.  Round 11 added controlled libc6
     * pre-unpack handling.  For packages with preinst metadata we require
     * evidence that those semantics were actually applied. */
    snprintf(preinst, sizeof preinst, "%s/var/lib/dpkg/info/%s.preinst", target_root, package);
    if (path_exists(preinst)) {
        if (!strcmp(package, "libc6") && libc6_preunpack_attested(target_root)) {
            app_printf("pkg configure: libc6 pre-unpack diversion state verified\n");
        } else if (verified_noop_preinst(package, target_root)) {
            app_printf("pkg configure: %s controlled fresh-install preinst state verified\n", package);
        } else {
            app_printf("pkg configure: UNSUPPORTED: %s has preinst without verified controlled pre-unpack state\n", package);
            app_printf("pkg configure: package remains unpacked\n");
            return 1;
        }
    }

    if (!strcmp(package, "libc6")) {
        if (process_libc6_postinst_fresh(target_root) != 0) {
            app_printf("pkg configure: package remains unpacked\n");
            return 1;
        }
    } else if (!strcmp(package, "libc-bin")) {
        if (process_libc_bin_postinst_fresh(target_root) != 0) {
            app_printf("pkg configure: package remains unpacked\n"); return 1;
        }
    } else if (!strcmp(package, "libpam-runtime")) {
        if (configure_libpam_runtime(target_root) != 0) { app_printf("pkg configure: package remains unpacked\n"); return 1; }
    } else if (!strcmp(package, "libpam-modules")) {
        if (configure_libpam_modules(target_root) != 0) { app_printf("pkg configure: package remains unpacked\n"); return 1; }
    } else if (!strcmp(package, "util-linux")) {
        if (configure_util_linux(target_root) != 0) { app_printf("pkg configure: package remains unpacked\n"); return 1; }
    } else if (!strcmp(package,"login") || !strcmp(package,"login.defs") ||
               !strcmp(package,"libpam0g") || !strcmp(package,"libpam-modules-bin")) {
        int nr=known_noop_postinst(package,target_root);
        if(nr<0){app_printf("pkg configure: unsupported %s postinst signature; package remains unpacked\n",package);return 1;}
        if(nr==0 && process_postinst_fresh(package,target_root)!=0){app_printf("pkg configure: package remains unpacked\n");return 1;}
    } else if (process_postinst_fresh(package, target_root) != 0) {
        app_printf("pkg configure: package remains unpacked\n"); return 1;
    }
    if (pkg_triggers_activate_package(package, target_root) != 0) {
        app_printf("pkg configure: package remains unpacked\n");
        return 1;
    }
    if (pkg_state_set_installed(target_root, package) != 0) {
        app_printf("pkg configure: failed to update dpkg status for %s\n", package);
        return 1;
    }
    app_printf("pkg configure: recorded %s %s as installed\n", st.package, st.version);
    app_printf("pkg configure: configuration complete\n");
    return 0;
}
