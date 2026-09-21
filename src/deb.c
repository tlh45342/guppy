#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include "deb.h"

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
    if (n && s[n-1] == '/') s[--n] = 0;
}
static int dec(const char *p, size_t n, uint64_t *v) {
    char b[32], *e; unsigned long long x;
    if (n >= sizeof b) return -1;
    memcpy(b,p,n); b[n]=0; trim(b); if (!b[0]) return -1;
    x=strtoull(b,&e,10); if (*e) return -1; *v=(uint64_t)x; return 0;
}
int deb_open_fp(FILE *f, deb_package_t *pkg) {
    char magic[8];
    if (!f || !pkg) return -1;
    memset(pkg,0,sizeof *pkg);
    if(fseek(f,0,SEEK_SET)) return -2;
    if(fread(magic,1,8,f)!=8 || memcmp(magic,"!<arch>\n",8)) return -3;
    for(;;){
        unsigned char h[60]; char name[17]; uint64_t sz,next; long pos;
        size_t n=fread(h,1,60,f); if(!n && feof(f))break; if(n!=60)return -4;
        if(h[58]!='`'||h[59]!='\n')return -5;
        memcpy(name,h,16);name[16]=0;trim(name); if(dec((char*)h+48,10,&sz))return -6;
        pos=ftell(f); if(pos<0)return -7;
        if(!strcmp(name,"debian-binary")){
            size_t take=sz<sizeof(pkg->version)-1?(size_t)sz:sizeof(pkg->version)-1;
            if(take && fread(pkg->version,1,take,f)!=take)return -8;
            pkg->version[take]=0;trim(pkg->version);
        } else if(!strncmp(name,"control.tar",11)){
            snprintf(pkg->control.name,sizeof pkg->control.name,"%s",name);pkg->control.offset=(uint64_t)pos;pkg->control.size=sz;
        } else if(!strncmp(name,"data.tar",8)){
            snprintf(pkg->data.name,sizeof pkg->data.name,"%s",name);pkg->data.offset=(uint64_t)pos;pkg->data.size=sz;
        }
        next=(uint64_t)pos+sz+(sz&1u); if(next>LONG_MAX||fseek(f,(long)next,SEEK_SET))return -9;
    }
    return (pkg->version[0]&&pkg->control.name[0]&&pkg->data.name[0])?0:-10;
}

int deb_open(const char *path, deb_package_t *pkg) {
    FILE *f;
    int rc;
    if (!path || !pkg) return -1;
    f=fopen(path,"rb"); if(!f) return -2;
    rc=deb_open_fp(f,pkg);
    fclose(f);
    return rc;
}
int deb_copy_member(FILE *fp, const deb_member_t *m, FILE *out) {
    unsigned char b[32768]; uint64_t left;
    if(!fp||!m||!out||m->offset>LONG_MAX||fseek(fp,(long)m->offset,SEEK_SET))return -1;
    left=m->size; while(left){size_t want=left<sizeof b?(size_t)left:sizeof b;size_t n=fread(b,1,want,fp);if(n!=want)return -1;if(fwrite(b,1,n,out)!=n)return -1;left-=n;} return 0;
}
