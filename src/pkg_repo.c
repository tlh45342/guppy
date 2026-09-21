#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pkg_repo.h"
#include "deb_version.h"
#include "gu_dirent.h"
#include "gu_path.h"
#include "gu_xz.h"
#include "gu_gzip.h"
#include "app_printf.h"

static pkg_repo_entry_t *g_entries;
static size_t g_count, g_cap;
static char g_roots[PKG_REPO_MAX][512];
static unsigned g_root_count;

static void seterr(char *b,size_t n,const char *s){if(b&&n)snprintf(b,n,"%s",s);}
static int ends(const char *s,const char *x){size_t a=strlen(s),b=strlen(x);return a>=b&&!strcmp(s+a-b,x);}
static int join(char *o,size_t n,const char *a,const char *b){int k=snprintf(o,n,"%s%s%s",a,(a[0]&&a[strlen(a)-1]!='/')?"/":"",b);return k<0||(size_t)k>=n?-1:0;}

static void copytrim(char *d,size_t cap,const char *a,size_t n){while(n&&isspace((unsigned char)*a)){a++;n--;}while(n&&isspace((unsigned char)a[n-1]))n--;if(n>=cap)n=cap-1;memcpy(d,a,n);d[n]=0;}
static void field(pkg_repo_entry_t *e,const char *k,size_t kn,const char *v,size_t vn){
#define F(name,member) if(kn==sizeof(name)-1&&!memcmp(k,name,sizeof(name)-1)){copytrim(e->member,sizeof e->member,v,vn);return;}
    F("Package",package) F("Version",version) F("Architecture",architecture)
    F("Depends",depends) F("Pre-Depends",pre_depends) F("Provides",provides)
    F("Conflicts",conflicts) F("Breaks",breaks) F("Replaces",replaces)
    F("Filename",filename) F("SHA256",sha256)
#undef F
    if(kn==4&&!memcmp(k,"Size",4)){char b[64];copytrim(b,sizeof b,v,vn);e->size=(uint64_t)strtoull(b,NULL,10);}
}

static int same_record(const pkg_repo_entry_t *a,const pkg_repo_entry_t *b,const char *root){
    return !strcmp(a->repo_root,root) &&
           !strcmp(a->package,b->package) &&
           !strcmp(a->version,b->version) &&
           !strcmp(a->architecture,b->architecture) &&
           !strcmp(a->filename,b->filename);
}

static int append_entry(pkg_repo_entry_t *e,const char *root,const char *index){
    pkg_repo_entry_t *p; size_t i;
    if(!e->package[0]||!e->version[0]||!e->filename[0])return 0;

    /* Mirrors and ISO trees may expose the same stanza through multiple suite
     * aliases or index encodings.  Do not turn those into duplicate planner
     * candidates for one registered repository root. */
    for(i=0;i<g_count;i++) if(same_record(&g_entries[i],e,root)) return 0;

    if(g_count==g_cap){size_t nc=g_cap?g_cap*2:256;p=(pkg_repo_entry_t*)realloc(g_entries,nc*sizeof(*p));if(!p)return -1;g_entries=p;g_cap=nc;}
    snprintf(e->repo_root,sizeof e->repo_root,"%s",root);snprintf(e->index_path,sizeof e->index_path,"%s",index);g_entries[g_count++]=*e;return 0;
}

static int parse_index(FILE *f,const char *root,const char *path){
    char *buf=NULL; size_t len=0,cap=0; unsigned char tmp[32768]; size_t n; int rc=0;
    while((n=fread(tmp,1,sizeof tmp,f))>0){char *nb;if(len+n+1>cap){size_t nc=cap?cap*2:65536;while(nc<len+n+1)nc*=2;nb=(char*)realloc(buf,nc);if(!nb){free(buf);return -1;}buf=nb;cap=nc;}memcpy(buf+len,tmp,n);len+=n;}
    if(ferror(f)){free(buf);return -1;}
    if(!buf){buf=(char*)malloc(1);if(!buf)return -1;}
    buf[len]=0;
    {
        size_t pos=0; pkg_repo_entry_t e; memset(&e,0,sizeof e);
        while(pos<len){
            size_t st=pos,en,col;
            while(pos<len&&buf[pos]!='\n')pos++;
            en=pos; if(pos<len)pos++;
            if(en>st&&buf[en-1]=='\r')en--;
            if(en==st){if(append_entry(&e,root,path)){rc=-1;break;}memset(&e,0,sizeof e);continue;}
            if(buf[st]==' '||buf[st]=='\t') continue;
            col=st; while(col<en&&buf[col]!=':')col++;
            if(col<en)field(&e,buf+st,col-st,buf+col+1,en-col-1);
        }
        if(!rc && append_entry(&e,root,path)) rc=-1;
    }
    free(buf); return rc;
}
static int load_index(const char *root,const char *path,char *err,size_t errsz){FILE *f=gu_path_open_seekable_read(path,NULL),*data=f;int close_data=0,rc;if(!f){seterr(err,errsz,"cannot open Packages index");return -1;}
    if(ends(path,".xz")){long sz;char xb[128];if(fseek(f,0,SEEK_END)|| (sz=ftell(f))<0 || fseek(f,0,SEEK_SET)){fclose(f);return -1;}data=gu_xz_decode_member(f,0,(uint64_t)sz,xb,sizeof xb);if(!data){fclose(f);seterr(err,errsz,xb);return -1;}close_data=1;}
    else if(ends(path,".gz")){long sz;char gb[128];if(fseek(f,0,SEEK_END)|| (sz=ftell(f))<0 || fseek(f,0,SEEK_SET)){fclose(f);return -1;}data=gu_gzip_decode_member(f,0,(uint64_t)sz,gb,sizeof gb);if(!data){fclose(f);seterr(err,errsz,gb);return -1;}close_data=1;}
    rc=parse_index(data,root,path);if(close_data)fclose(data);fclose(f);return rc;}

static int scan_dir(const char *root,const char *path,int depth,unsigned *found,char *err,size_t errsz){DIR*d;struct dirent*de;if(depth>6)return 0;d=vfs_opendir(path);if(!d)return -1;while((de=vfs_readdir(d))){char p[1024];if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;if(join(p,sizeof p,path,de->d_name))continue;if(!strcmp(de->d_name,"Packages")||!strcmp(de->d_name,"Packages.xz")||!strcmp(de->d_name,"Packages.gz")){
            if ((strstr(p,"/binary-amd64/Packages") || strstr(p,"/binary-all/Packages")) &&
                !strstr(p,"/debian-installer/")) {
                if(load_index(root,p,err,errsz)==0)(*found)++;
            }
        }
        else if(de->d_type==VFS_DT_DIR||de->d_type==VFS_DT_UNKNOWN) (void)scan_dir(root,p,depth+1,found,err,errsz);
    }vfs_closedir(d);return 0;}

static int root_index(const char *root){unsigned i;for(i=0;i<g_root_count;i++)if(!strcmp(g_roots[i],root))return (int)i;return -1;}

int pkg_repo_add(const char *root,char *err,size_t errsz){char dists[768];unsigned found=0;size_t before=g_count;if(!root||!*root)return -1;if(root_index(root)>=0){seterr(err,errsz,"repository is already registered");return -1;}if(g_root_count>=PKG_REPO_MAX){seterr(err,errsz,"too many repositories");return -1;}if(join(dists,sizeof dists,root,"dists")){seterr(err,errsz,"repository path too long");return -1;}if(scan_dir(root,dists,0,&found,err,errsz)!=0||!found){g_count=before;seterr(err,errsz,"no readable Packages, Packages.gz, or Packages.xz index under dists/");return -1;}snprintf(g_roots[g_root_count++],sizeof g_roots[0],"%s",root);return (int)(g_count-before);}

int pkg_repo_delete(const char *spec,char *err,size_t errsz){
    int idx=-1; unsigned i; char *end=NULL; unsigned long n; size_t wr;
    if(!spec||!*spec){seterr(err,errsz,"missing repository number or root");return -1;}
    n=strtoul(spec,&end,10);
    if(end&&*end==0&&n>=1&&n<=g_root_count) idx=(int)n-1;
    if(idx<0) idx=root_index(spec);
    if(idx<0){seterr(err,errsz,"repository not found");return -1;}

    /* Remove package records belonging to this root, then compact roots. */
    wr=0;
    for(i=0;i<g_count;i++){
        if(strcmp(g_entries[i].repo_root,g_roots[idx])){
            if(wr!=(size_t)i) g_entries[wr]=g_entries[i];
            wr++;
        }
    }
    g_count=wr;
    for(i=(unsigned)idx+1;i<g_root_count;i++) snprintf(g_roots[i-1],sizeof g_roots[i-1],"%s",g_roots[i]);
    g_root_count--;
    return 0;
}

void pkg_repo_list(void){unsigned i;app_printf("Repositories: %u\n",g_root_count);for(i=0;i<g_root_count;i++){size_t j,n=0;for(j=0;j<g_count;j++)if(!strcmp(g_entries[j].repo_root,g_roots[i]))n++;app_printf("  %u  %s  (%llu records)\n",i+1,g_roots[i],(unsigned long long)n);}app_printf("Indexed packages: %llu\n",(unsigned long long)g_count);}
void pkg_repo_clear(void){free(g_entries);g_entries=NULL;g_count=g_cap=0;g_root_count=0;}
size_t pkg_repo_entry_count(void){return g_count;} const pkg_repo_entry_t*pkg_repo_entry_at(size_t i){return i<g_count?&g_entries[i]:NULL;}
const pkg_repo_entry_t*pkg_repo_find_best(const char*name,const char*op,const char*ver){const pkg_repo_entry_t*best=NULL;size_t i;for(i=0;i<g_count;i++){const pkg_repo_entry_t*e=&g_entries[i];if(strcmp(e->package,name))continue;if(!deb_version_satisfies(e->version,op,ver))continue;if(!best||deb_version_compare(e->version,best->version)>0)best=e;}return best;}
static int provides_name(const char*s,const char*n){size_t nl=strlen(n);while(s&&*s){while(*s==' '||*s=='\t'||*s==',')s++;if(!strncmp(s,n,nl)&&(s[nl]==0||s[nl]==','||s[nl]==' '||s[nl]=='\t'||s[nl]=='('))return 1;s=strchr(s,',');if(s)s++;}return 0;}
const pkg_repo_entry_t*pkg_repo_find_provider(const char*n){const pkg_repo_entry_t*best=NULL;size_t i;for(i=0;i<g_count;i++)if(provides_name(g_entries[i].provides,n)&&(!best||deb_version_compare(g_entries[i].version,best->version)>0))best=&g_entries[i];return best;}

int pkg_repo_resolve_path(const char *name,char *out,size_t outsz){const pkg_repo_entry_t*e=pkg_repo_find_best(name,NULL,NULL);if(!e||!out||!outsz)return -1;return join(out,outsz,e->repo_root,e->filename);}
int pkg_repo_path_print(const char*n){char path[1400];if(pkg_repo_resolve_path(n,path,sizeof path)){app_printf("pkg: package '%s' not found in registered repositories\n",n);return 1;}app_printf("%s\n",path);return 0;}
int pkg_repo_lookup_print(const char*n){const pkg_repo_entry_t*e=pkg_repo_find_best(n,NULL,NULL);char path[1400];if(!e){app_printf("pkg: package '%s' not found in registered repositories\n",n);return 1;}app_printf("Package: %s\nVersion: %s\nArchitecture: %s\nFilename: %s\n",e->package,e->version,e->architecture,e->filename);if(!pkg_repo_resolve_path(n,path,sizeof path))app_printf("Path: %s\n",path);if(e->size)app_printf("Size: %llu\n",(unsigned long long)e->size);if(e->sha256[0])app_printf("SHA256: %s\n",e->sha256);if(e->pre_depends[0])app_printf("Pre-Depends: %s\n",e->pre_depends);if(e->depends[0])app_printf("Depends: %s\n",e->depends);if(e->provides[0])app_printf("Provides: %s\n",e->provides);app_printf("Index: %s\n",e->index_path);return 0;}
