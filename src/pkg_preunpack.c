#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pkg_preunpack.h"
#include "vfs.h"
#include "vfs_stat.h"
#include "app_printf.h"

#define PU_PATH_MAX 1400

typedef struct sb { char *p; size_t n, cap; } sb_t;
static int grow(sb_t*b,size_t add){size_t c;char*np;if(b->n+add+1<=b->cap)return 0;c=b->cap?b->cap:512;while(c<b->n+add+1)c*=2;np=(char*)realloc(b->p,c);if(!np)return 1;b->p=np;b->cap=c;return 0;}
static int add(sb_t*b,const char*s,size_t n){if(grow(b,n))return 1;memcpy(b->p+b->n,s,n);b->n+=n;b->p[b->n]=0;return 0;}
static int readfile(const char*p,char**out,size_t*n){struct file*f=NULL;sb_t b={0};if(vfs_open(p,VFS_O_RDONLY,0,&f)!=0)return 1;for(;;){char t[1024];ssize_t r=vfs_read(f,t,sizeof t);if(r<0){vfs_close(f);free(b.p);return 1;}if(!r)break;if(add(&b,t,(size_t)r)){vfs_close(f);free(b.p);return 1;}}vfs_close(f);if(!b.p){b.p=(char*)calloc(1,1);if(!b.p)return 1;}*out=b.p;if(n)*n=b.n;return 0;}
static int parents(const char*p){char b[PU_PATH_MAX];size_t i,n=strlen(p);if(n>=sizeof b)return 1;memcpy(b,p,n+1);for(i=1;i<n;i++)if(b[i]=='/'){struct g_stat st;b[i]=0;if(vfs_mkdir(b,0755)!=0&&vfs_stat(b,&st)!=0){b[i]='/';return 1;}b[i]='/';}return 0;}
static int writefile(const char*p,const char*d,size_t n){struct file*f=NULL;size_t o=0;if(parents(p)||vfs_open(p,VFS_O_WRONLY|VFS_O_CREAT|VFS_O_TRUNC,0644,&f)!=0)return 1;while(o<n){ssize_t w=vfs_write(f,d+o,n-o);if(w<=0){vfs_close(f);return 1;}o+=(size_t)w;}return vfs_close(f)!=0;}
static int readlink_text(const char*p,char*out,size_t cap){ssize_t n=vfs_readlink(p,out,cap);if(n<=0||(size_t)n>=cap)return 1;out[n]=0;return 0;}
static int islink(const char*p){char b[PU_PATH_MAX];return vfs_readlink(p,b,sizeof b)>0;}
static int exists(const char*p){struct g_stat st;return vfs_stat(p,&st)==0||islink(p);}
static int isdir(const char*p){struct g_stat st;return vfs_stat(p,&st)==0 && VFS_S_ISDIR(st.st_mode);}

static int diversion_add(const char*root,const char*from,const char*to,const char*owner)
{
    char path[PU_PATH_MAX], *old=NULL; size_t oldn=0; sb_t b={0};
    char rec[PU_PATH_MAX*2+256]; int nw;
    snprintf(path,sizeof path,"%s/var/lib/dpkg/diversions",root);
    nw=snprintf(rec,sizeof rec,"%s\n%s\n%s\n",from,to,owner);
    if(nw<0||(size_t)nw>=sizeof rec)return 1;
    if(readfile(path,&old,&oldn)==0){
        char *scan=old;
        /* dpkg's diversions file is a sequence of three-line records.  Refuse
         * a conflicting record for the same source instead of silently
         * appending a second meaning for one pathname. */
        while(*scan){
            char *a=scan,*b1,*c,*e;
            b1=strchr(a,'\n'); if(!b1)break; *b1++=0;
            c=strchr(b1,'\n'); if(!c)break; *c++=0;
            e=strchr(c,'\n'); if(e)*e=0;
            if(!strcmp(a,from)){
                if(!strcmp(b1,to)&&!strcmp(c,owner)){
                    free(old); app_printf("pkg pre-unpack: diversion already registered: %s\n",from); return 0;
                }
                app_printf("pkg pre-unpack: REFUSED conflicting diversion for %s\n",from);
                free(old); return 1;
            }
            if(!e)break;
            scan=e+1;
        }
        /* Re-read because the parser temporarily inserted NULs. */
        free(old); old=NULL; oldn=0;
        if(readfile(path,&old,&oldn)!=0)return 1;
        if(add(&b,old,oldn)){free(old);return 1;} free(old);
        if(b.n&&b.p[b.n-1]!='\n'&&add(&b,"\n",1))return 1;
    }
    if(add(&b,rec,(size_t)nw)){free(b.p);return 1;}
    if(writefile(path,b.p,b.n)){free(b.p);return 1;} free(b.p);
    app_printf("pkg pre-unpack: diversion add --no-rename: %s -> %s (owner %s)\n",from,to,owner);
    return 0;
}

static int base_files_signature_ok(const char *preinst)
{
    return strstr(preinst,"for d in bin lib lib32 lib64 libo32 libx32 sbin; do") &&
           strstr(preinst,"target=$(readlink \"$DPKG_ROOT/$d\")") &&
           strstr(preinst,"Fixing bad /$d symbolic link created by systemd-nspawn") &&
           strstr(preinst,"dpkg-divert --quiet --package base-files --add --no-rename") &&
           strstr(preinst,"/.$d.usr-is-merged");
}

static int base_files_check_one(const char *root,const char *d)
{
    char p[PU_PATH_MAX],u[PU_PATH_MAX],want[128],got[PU_PATH_MAX];
    snprintf(p,sizeof p,"%s/%s",root,d);
    snprintf(u,sizeof u,"%s/usr/%s",root,d);
    snprintf(want,sizeof want,"usr/%s",d);

    if(islink(p)){
        if(readlink_text(p,got,sizeof got))return 1;
        if(strcmp(got,want)!=0){
            /* Debian 13 carries one narrowly-scoped workaround for the bad
             * /lib64 -> usr/lib/<multiarch> link systemd-nspawn could create.
             * On amd64 the proper merged-/usr alias is /lib64 -> usr/lib64. */
            if(!strcmp(d,"lib64") && !strncmp(got,"usr/lib/",8)){
                app_printf("pkg pre-unpack: base-files fixing bad /lib64 symlink: %s\n",got);
                if(vfs_unlink(p)!=0)return 1;
                if(isdir(u)){
                    if(vfs_symlink(want,p)!=0)return 1;
                    app_printf("pkg pre-unpack: merged-/usr link fixed: /lib64 -> usr/lib64\n");
                } else {
                    app_printf("pkg pre-unpack: removed bad dangling /lib64 symlink\n");
                    return 0;
                }
            } else {
                app_printf("pkg pre-unpack: REFUSED: /%s symlink target is '%s', expected '%s'\n",d,got,want);
                return 1;
            }
        }
        if(islink(p) && !isdir(u)){
            app_printf("pkg pre-unpack: REFUSED: /%s is a dangling merged-/usr symlink\n",d);
            return 1;
        }
        return 0;
    }
    if(isdir(p)){
        app_printf("pkg pre-unpack: REFUSED: /%s is a directory; merged-/usr symlink required\n",d);
        return 1;
    }
    if(exists(p)){
        app_printf("pkg pre-unpack: REFUSED: /%s has unexpected existing type\n",d);
        return 1;
    }
    return 0;
}

static int preinst_heredoc(const char *preinst,const char *path,const char **data,size_t *len)
{
    char marker[256]; const char *p,*start,*end; int n;
    n=snprintf(marker,sizeof marker,"cat > \"$DPKG_ROOT%s\" <<'EOF'",path);
    if(n<0||(size_t)n>=sizeof marker)return 1;
    p=strstr(preinst,marker); if(!p)return 1;
    p+=strlen(marker);
    if(*p=='\r')p++;
    if(*p!='\n')return 1;
    start=p+1;
    end=strstr(start,"\nEOF");
    if(!end)return 1;
    /* Require EOF to be a complete delimiter line. */
    if(end[4] && end[4]!='\n' && end[4]!='\r')return 1;
    *data=start; *len=(size_t)(end-start);
    if(*len && start[*len-1]!='\n')(*len)++; /* normally unreachable; keep cat heredoc newline semantics */
    return 0;
}

static int base_passwd_signature_ok(const char *preinst)
{
    return strstr(preinst,"if [ \"$1\" = install ]; then") &&
           strstr(preinst,"if [ ! -e \"$DPKG_ROOT/etc/passwd\" ]; then") &&
           strstr(preinst,"cat > \"$DPKG_ROOT/etc/passwd\" <<'EOF'") &&
           strstr(preinst,"if [ ! -e \"$DPKG_ROOT/etc/group\" ]; then") &&
           strstr(preinst,"cat > \"$DPKG_ROOT/etc/group\" <<'EOF'") &&
           strstr(preinst,"exit 0");
}

static int base_passwd_fresh(const char *root,const char *preinst)
{
    const char *passwd_data=NULL,*group_data=NULL; size_t passwd_len=0,group_len=0;
    char passwd_path[PU_PATH_MAX],group_path[PU_PATH_MAX]; struct g_stat st;
    if(!base_passwd_signature_ok(preinst) ||
       preinst_heredoc(preinst,"/etc/passwd",&passwd_data,&passwd_len) ||
       preinst_heredoc(preinst,"/etc/group",&group_data,&group_len)) {
        app_printf("pkg pre-unpack: UNSUPPORTED base-passwd preinst signature; payload was NOT unpacked\n");
        return 1;
    }
    snprintf(passwd_path,sizeof passwd_path,"%s/etc/passwd",root);
    snprintf(group_path,sizeof group_path,"%s/etc/group",root);

    if(vfs_stat(passwd_path,&st)!=0) {
        if(islink(passwd_path)) {
            app_printf("pkg pre-unpack: REFUSED dangling /etc/passwd symlink\n");
            return 1;
        }
        if(writefile(passwd_path,passwd_data,passwd_len))return 1;
        app_printf("pkg pre-unpack: base-passwd created /etc/passwd from package master data\n");
    } else {
        app_printf("pkg pre-unpack: base-passwd preserved existing /etc/passwd\n");
    }
    if(vfs_stat(group_path,&st)!=0) {
        if(islink(group_path)) {
            app_printf("pkg pre-unpack: REFUSED dangling /etc/group symlink\n");
            return 1;
        }
        if(writefile(group_path,group_data,group_len))return 1;
        app_printf("pkg pre-unpack: base-passwd created /etc/group from package master data\n");
    } else {
        app_printf("pkg pre-unpack: base-passwd preserved existing /etc/group\n");
    }
    app_printf("pkg pre-unpack: base-passwd fresh-install actions complete\n");
    return 0;
}

static int dash_signature_ok(const char *preinst)
{
    /* Debian dash 0.5.12-12: the preinst has no fresh-install action.  Its
     * only substantive branch is an upgrade guard for versions older than
     * 0.5.12-9~.  We recognize that audited structure rather than executing
     * /bin/sh or trying to emulate the upgrade path. */
    return strstr(preinst,"if [ \"$1\" = upgrade ] && dpkg --compare-versions \"$2\" lt 0.5.12-9~; then") &&
           strstr(preinst,"dpkg-divert --truename /bin/sh") &&
           strstr(preinst,"dash/ineffectivediversion") &&
           strstr(preinst,"Aborting upgrade. Resolve the issue and try again.") &&
           strstr(preinst,"exit 0");
}

static int dash_fresh(const char *preinst)
{
    if(!dash_signature_ok(preinst)) {
        app_printf("pkg pre-unpack: UNSUPPORTED dash preinst signature; payload was NOT unpacked\n");
        return 1;
    }
    app_printf("pkg pre-unpack: dash fresh-install preinst has no actions; audited no-op\n");
    app_printf("pkg pre-unpack: dash fresh-install actions complete\n");
    return 0;
}

static int login_fresh(const char *preinst)
{
    if (!strstr(preinst,"dpkg-maintscript-helper rm_conffile /etc/securetty") ||
        !strstr(preinst,"dpkg-maintscript-helper rm_conffile /etc/login.defs")) {
        app_printf("pkg pre-unpack: UNSUPPORTED login preinst signature; payload was NOT unpacked\n");
        return 1;
    }
    app_printf("pkg pre-unpack: login fresh-install conffile migrations are not applicable; audited no-op\n");
    app_printf("pkg pre-unpack: login fresh-install actions complete\n");
    return 0;
}

static int libpam_modules_fresh(const char *preinst)
{
    if (!strstr(preinst,". /usr/share/debconf/confmodule") ||
        !strstr(preinst,"handle_profiles_with_removed_modules()") ||
        !strstr(preinst,"dpkg --compare-versions \"$2\" lt-nl 1.4.0-5") ||
        !strstr(preinst,"libpam-modules/disable-screensaver")) {
        app_printf("pkg pre-unpack: UNSUPPORTED libpam-modules preinst signature; payload was NOT unpacked\n");
        return 1;
    }
    app_printf("pkg pre-unpack: libpam-modules fresh-install upgrade/profile-removal branches are not applicable; audited no-op\n");
    app_printf("pkg pre-unpack: libpam-modules fresh-install actions complete\n");
    return 0;
}

static int gzip_signature_ok(const char *preinst)
{
    /* Debian gzip 1.13-1 carries a temporary zutils diversion migration.
     * On a pristine target there is no pre-/usr-move zutils diversion, so
     * the fresh-install path is an audited no-op.  Keep the recognition
     * deliberately narrow so a changed maintainer script fails closed. */
    return strstr(preinst,"begin-remove-after: released:forky") &&
           strstr(preinst,"if [ \"$1\" = upgrade ] || [ \"$1\" = install ]; then") &&
           strstr(preinst,"for tool in zcat zcmp zdiff zegrep zfgrep zgrep; do") &&
           strstr(preinst,"dpkg-divert --truename \"/bin/$tool\"") &&
           strstr(preinst,"dpkg-divert --listpackage \"/bin/$tool\"") &&
           strstr(preinst,"/bin/$tool.gzip") &&
           strstr(preinst,"dpkg-divert --truename \"/usr/bin/$tool\"") &&
           strstr(preinst,"--divert \"/usr/bin/$tool.usr-is-merged\"") &&
           strstr(preinst,"--add \"/usr/bin/$tool\"");
}

static int diversion_source_exists(const char *root,const char *from)
{
    char path[PU_PATH_MAX], *buf=NULL, *scan;
    snprintf(path,sizeof path,"%s/var/lib/dpkg/diversions",root);
    if(readfile(path,&buf,NULL)!=0)return 0;
    scan=buf;
    while(*scan){
        char *a=scan,*b,*c,*e;
        b=strchr(a,'\n'); if(!b)break; *b++=0;
        c=strchr(b,'\n'); if(!c)break; *c++=0;
        e=strchr(c,'\n'); if(e)*e=0;
        if(!strcmp(a,from)){free(buf);return 1;}
        if(!e)break;
        scan=e+1;
    }
    free(buf); return 0;
}

static int gzip_fresh(const char *root,const char *preinst)
{
    static const char *tools[]={"zcat","zcmp","zdiff","zegrep","zfgrep","zgrep"};
    size_t i;
    if(!gzip_signature_ok(preinst)){
        app_printf("pkg pre-unpack: UNSUPPORTED gzip preinst signature; payload was NOT unpacked\n");
        return 1;
    }
    /* The real script only acts when zutils owns a historical /bin diversion.
     * Guppy does not yet emulate dpkg-divert --truename/listpackage queries, so
     * refuse any legacy source diversion rather than guessing about migration. */
    for(i=0;i<sizeof(tools)/sizeof(tools[0]);i++){
        char from[96];
        snprintf(from,sizeof from,"/bin/%s",tools[i]);
        if(diversion_source_exists(root,from)){
            app_printf("pkg pre-unpack: REFUSED gzip historical diversion state at %s; zutils migration not implemented\n",from);
            return 1;
        }
    }
    app_printf("pkg pre-unpack: gzip: no applicable zutils diversions; audited no-op\n");
    app_printf("pkg pre-unpack: gzip fresh-install actions complete\n");
    return 0;
}

static int base_files_fresh(const char *root,const char *preinst)
{
    static const char *check_dirs[]={"bin","lib","lib32","lib64","libo32","libx32","sbin"};
    static const char *multilib[]={"lib32","libo32","libx32"};
    size_t i;
    if(!base_files_signature_ok(preinst)){
        app_printf("pkg pre-unpack: UNSUPPORTED base-files preinst signature; payload was NOT unpacked\n");
        return 1;
    }
    for(i=0;i<sizeof(check_dirs)/sizeof(check_dirs[0]);i++)
        if(base_files_check_one(root,check_dirs[i]))return 1;

    /* amd64 base-files 13.8 builds USR_MERGE as bin/lib/sbin/lib64; the
     * remaining multilib aliases receive DEP17 M4 protective diversions. */
    for(i=0;i<sizeof(multilib)/sizeof(multilib[0]);i++){
        char from[64],to[96];
        snprintf(from,sizeof from,"/%s",multilib[i]);
        snprintf(to,sizeof to,"/.%s.usr-is-merged",multilib[i]);
        if(diversion_add(root,from,to,"base-files"))return 1;
    }
    app_printf("pkg pre-unpack: base-files merged-/usr checks passed\n");
    app_printf("pkg pre-unpack: base-files fresh-install actions complete\n");
    return 0;
}

int pkg_preunpack_fresh(const char *package,const char *version,const char *root,
                        const char *preinst,size_t preinst_len)
{
    char lib[PU_PATH_MAX],lib64[PU_PATH_MAX],usr64[PU_PATH_MAX];
    (void)version;(void)preinst_len;
    if(!preinst||!*preinst)return 0;
    app_printf("pkg pre-unpack: %s: controlled fresh-install preinst\n",package);
    app_printf("pkg pre-unpack: Shell execution: DISABLED\n");

    if(!strcmp(package,"base-files"))
        return base_files_fresh(root,preinst);
    if(!strcmp(package,"base-passwd"))
        return base_passwd_fresh(root,preinst);
    if(!strcmp(package,"dash"))
        return dash_fresh(preinst);
    if(!strcmp(package,"gzip"))
        return gzip_fresh(root,preinst);
    if(!strcmp(package,"login"))
        return login_fresh(preinst);
    if(!strcmp(package,"libpam-modules"))
        return libpam_modules_fresh(preinst);

    /* libc6 remains a narrowly audited Debian 13.6 amd64 path. */
    if(strcmp(package,"libc6")!=0 ||
       !strstr(preinst,"rtlddir=/lib64") ||
       !strstr(preinst,"dpkg-divert --quiet --add --no-rename") ||
       !strstr(preinst,"/lib64/ld-linux-x86-64.so.2.usr-is-merged")) {
        app_printf("pkg pre-unpack: UNSUPPORTED preinst for %s; payload was NOT unpacked\n",package);
        return 1;
    }

    snprintf(lib,sizeof lib,"%s/lib",root); snprintf(lib64,sizeof lib64,"%s/lib64",root);
    snprintf(usr64,sizeof usr64,"%s/usr/lib64",root);
    if(islink(lib)) {
        if(!exists(lib64)) {
            if(parents(usr64)||vfs_mkdir(usr64,0755)!=0) { struct g_stat st; if(vfs_stat(usr64,&st)!=0)return 1; }
            if(vfs_symlink("usr/lib64",lib64)!=0)return 1;
            app_printf("pkg pre-unpack: merged-/usr link created: /lib64 -> usr/lib64\n");
        }
    }
    if(exists(lib64) && !islink(lib64)) {
        app_printf("pkg pre-unpack: REFUSED: /lib64 is a directory; merged-/usr layout required\n");
        return 1;
    }

    /* base-files is absent from this fresh target, so libc6's audited condition
     * installs the DEP17 M4 diversion on behalf of base-files. */
    if(diversion_add(root,"/lib64","/.lib64.usr-is-merged","base-files"))return 1;
    if(diversion_add(root,"/lib64/ld-linux-x86-64.so.2",
                     "/lib64/ld-linux-x86-64.so.2.usr-is-merged","libc6"))return 1;
    app_printf("pkg pre-unpack: libc6 fresh-install actions complete\n");
    return 0;
}
