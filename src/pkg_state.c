#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pkg_state.h"
#include "vfs.h"
#include "app_printf.h"

static int read_status(const char*root,char**out,size_t*len){char p[1024];struct file*f=NULL;char*b=NULL;size_t used=0,cap=0;snprintf(p,sizeof p,"%s/var/lib/dpkg/status",root);if(vfs_open(p,VFS_O_RDONLY,0,&f)!=0)return -1;for(;;){char t[8192];ssize_t n=vfs_read(f,t,sizeof t);if(n<0){free(b);vfs_close(f);return -1;}if(!n)break;if(used+(size_t)n+1>cap){size_t nc=cap?cap*2:16384;char*nb;while(nc<used+(size_t)n+1)nc*=2;nb=(char*)realloc(b,nc);if(!nb){free(b);vfs_close(f);return -1;}b=nb;cap=nc;}memcpy(b+used,t,(size_t)n);used+=(size_t)n;}vfs_close(f);if(!b){b=(char*)malloc(1);if(!b)return -1;}b[used]=0;*out=b;*len=used;return 0;}
static void cp(char*d,size_t cap,const char*a,size_t n){while(n&&isspace((unsigned char)*a)){a++;n--;}while(n&&isspace((unsigned char)a[n-1]))n--;if(n>=cap)n=cap-1;memcpy(d,a,n);d[n]=0;}
static void parse_para(const char*a,const char*b,pkg_state_entry_t*e){const char*p=a;memset(e,0,sizeof*e);while(p<b){const char*nl=memchr(p,'\n',(size_t)(b-p));const char*end=nl?nl:b;const char*c=memchr(p,':',(size_t)(end-p));if(c){size_t kn=(size_t)(c-p);if(kn==7&&!memcmp(p,"Package",7))cp(e->package,sizeof e->package,c+1,(size_t)(end-c-1));else if(kn==7&&!memcmp(p,"Version",7))cp(e->version,sizeof e->version,c+1,(size_t)(end-c-1));else if(kn==6&&!memcmp(p,"Status",6))cp(e->state,sizeof e->state,c+1,(size_t)(end-c-1));}p=nl?nl+1:b;}}
int pkg_state_lookup(const char*root,const char*pkg,pkg_state_entry_t*out){char*b;size_t n;const char*p,*e;if(read_status(root,&b,&n))return -1;p=b;e=b+n;while(p<e){const char*q=strstr(p,"\n\n");pkg_state_entry_t x;const char*end=q?q:e;parse_para(p,end,&x);if(!strcmp(x.package,pkg)){if(out)*out=x;free(b);return 0;}p=q?q+2:e;}free(b);return 1;}
int pkg_state_list(const char*root){char*b;size_t n;const char*p,*e;unsigned count=0;if(read_status(root,&b,&n)){app_printf("pkg: no package status database under %s\n",root);return 1;}app_printf("Package                          Version                         State\n");app_printf("------------------------------- ------------------------------- ----------------\n");p=b;e=b+n;while(p<e){const char*q=strstr(p,"\n\n");pkg_state_entry_t x;const char*end=q?q:e;parse_para(p,end,&x);if(x.package[0]){const char*s=x.state;const char*last=strrchr(s,' ');if(last&&last[1])s=last+1;app_printf("%-31s %-31s %s\n",x.package,x.version,s);count++;}p=q?q+2:e;}app_printf("Packages registered: %u\n",count);free(b);return 0;}


int pkg_state_set_installed(const char *root, const char *package)
{
    char *b = NULL, *out = NULL;
    size_t n = 0, cap = 0, used = 0;
    const char *p, *e;
    char status_path[1024];
    int found = 0;
    struct file *f = NULL;

    if (read_status(root, &b, &n) != 0) return -1;
    cap = n + 128;
    out = (char *)malloc(cap);
    if (!out) { free(b); return -1; }
    p = b; e = b + n;
    while (p < e) {
        const char *q = strstr(p, "\n\n");
        const char *end = q ? q : e;
        pkg_state_entry_t x;
        const char *lp;
        parse_para(p, end, &x);
        if (!strcmp(x.package, package)) {
            found = 1;
            lp = p;
            while (lp < end) {
                const char *nl = memchr(lp, '\n', (size_t)(end - lp));
                const char *le = nl ? nl : end;
                size_t ln;
                const char replacement[] = "Status: install ok installed\n";
                if ((size_t)(le - lp) >= 7 && !memcmp(lp, "Status:", 7)) {
                    ln = sizeof(replacement) - 1;
                    if (used + ln + 3 > cap) { cap = (used + ln + 3) * 2; out = (char *)realloc(out, cap); if (!out) { free(b); return -1; } }
                    memcpy(out + used, replacement, ln); used += ln;
                } else {
                    ln = (size_t)(le - lp);
                    if (used + ln + 4 > cap) { cap = (used + ln + 4) * 2; out = (char *)realloc(out, cap); if (!out) { free(b); return -1; } }
                    memcpy(out + used, lp, ln); used += ln;
                    if (nl) out[used++] = '\n';
                }
                lp = nl ? nl + 1 : end;
            }
        } else {
            size_t ln = (size_t)(end - p);
            if (used + ln + 4 > cap) { cap = (used + ln + 4) * 2; out = (char *)realloc(out, cap); if (!out) { free(b); return -1; } }
            memcpy(out + used, p, ln); used += ln;
            if (used == 0 || out[used-1] != '\n') out[used++] = '\n';
        }
        if (q) {
            if (used == 0 || out[used-1] != '\n') out[used++] = '\n';
            out[used++] = '\n';
        }
        p = q ? q + 2 : e;
    }
    free(b);
    if (!found) { free(out); return 1; }
    snprintf(status_path, sizeof status_path, "%s/var/lib/dpkg/status", root);
    if (vfs_open(status_path, VFS_O_WRONLY | VFS_O_TRUNC, 0644, &f) != 0) { free(out); return -1; }
    {
        size_t off = 0;
        while (off < used) {
            ssize_t wr = vfs_write(f, out + off, used - off);
            if (wr <= 0) { vfs_close(f); free(out); return -1; }
            off += (size_t)wr;
        }
    }
    vfs_close(f);
    free(out);
    return 0;
}
