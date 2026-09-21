// src/cmd_useradd.c — offline account creation under a mounted target root.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "cmds.h"
#include "vfs.h"
#include "account_util.h"
#include "app_printf.h"

static void usage(void)
{
    app_printf("usage: useradd -R <root> [-u UID] [-g GID] [-d HOME] [-s SHELL] [-c COMMENT] [-m|-M] <name>\n");
}

static int parse_u32(const char *s, uint32_t *out)
{
    char *e=NULL;
    unsigned long v;
    if(!s||!*s)return -1;
    v=strtoul(s,&e,10);
    if(!e||*e!='\0'||v>UINT32_MAX)return -1;
    *out=(uint32_t)v;
    return 0;
}

static int write_account_file(const char *root, const char *rel,
                              const char *text, uint32_t mode)
{
    char path[512];
    if(!account_join_path(path,sizeof path,root,rel)) return -1;
    if(!account_vfs_write_all(path,text,strlen(text),mode)) {
        fprintf(stderr,"useradd: cannot write '%s'\n",path);
        return -1;
    }
    (void)vfs_chmod(path,mode);
    return 0;
}

int cmd_useradd(int argc, char **argv)
{
    const char *root=NULL,*home=NULL,*shell="/bin/sh",*comment="";
    const char *name=NULL;
    uint32_t uid=0,gid=0;
    int uid_set=0,gid_set=0,create_home=1;

    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"-R") && i+1<argc) root=argv[++i];
        else if(!strcmp(argv[i],"-u") && i+1<argc) {
            if(parse_u32(argv[++i],&uid)!=0){fprintf(stderr,"useradd: invalid UID\n");return 2;}
            uid_set=1;
        }
        else if(!strcmp(argv[i],"-g") && i+1<argc) {
            if(parse_u32(argv[++i],&gid)!=0){fprintf(stderr,"useradd: invalid GID\n");return 2;}
            gid_set=1;
        }
        else if(!strcmp(argv[i],"-d") && i+1<argc) home=argv[++i];
        else if(!strcmp(argv[i],"-s") && i+1<argc) shell=argv[++i];
        else if(!strcmp(argv[i],"-c") && i+1<argc) comment=argv[++i];
        else if(!strcmp(argv[i],"-m")) create_home=1;
        else if(!strcmp(argv[i],"-M")) create_home=0;
        else if(argv[i][0]=='-'){usage();return 2;}
        else if(!name) name=argv[i];
        else {usage();return 2;}
    }

    if(!root||!name){usage();return 2;}
    if(!account_valid_name(name)){fprintf(stderr,"useradd: invalid user name '%s'\n",name);return 2;}

    char etc[512],ppath[512],spath[512],gpath[512],gspath[512];
    if(!account_join_path(etc,sizeof etc,root,"etc") ||
       !account_join_path(ppath,sizeof ppath,root,"etc/passwd") ||
       !account_join_path(spath,sizeof spath,root,"etc/shadow") ||
       !account_join_path(gpath,sizeof gpath,root,"etc/group") ||
       !account_join_path(gspath,sizeof gspath,root,"etc/gshadow")) return 1;
    if(!account_vfs_mkdir_p(etc,0755)){fprintf(stderr,"useradd: cannot create '%s'\n",etc);return 1;}

    char *passwd_text=NULL,*shadow_text=NULL,*group_text=NULL,*gshadow_text=NULL;
    if(!account_vfs_read_all(ppath,&passwd_text,NULL) ||
       !account_vfs_read_all(spath,&shadow_text,NULL) ||
       !account_vfs_read_all(gpath,&group_text,NULL) ||
       !account_vfs_read_all(gspath,&gshadow_text,NULL)) {
        fprintf(stderr,"useradd: cannot read account database\n");
        free(passwd_text);free(shadow_text);free(group_text);free(gshadow_text);
        return 1;
    }

    if(account_has_name(passwd_text,name)) {
        fprintf(stderr,"useradd: user '%s' already exists\n",name);
        free(passwd_text);free(shadow_text);free(group_text);free(gshadow_text);
        return 1;
    }

    if(!uid_set && account_next_uid(passwd_text,1000,&uid)!=0) {
        fprintf(stderr,"useradd: cannot allocate UID\n");
        goto fail;
    }
    if(!gid_set) gid=uid;

    char homebuf[160];
    if(!home) {
        snprintf(homebuf,sizeof homebuf,"/home/%s",name);
        home=homebuf;
    }
    if(home[0]!='/'){fprintf(stderr,"useradd: home must be an absolute target path\n");goto fail;}

    char pline[512],sline[256],gline[256],gsline[256];
    long days=(long)(time(NULL)/86400);
    snprintf(pline,sizeof pline,"%s:x:%u:%u:%s:%s:%s",name,uid,gid,comment,home,shell);
    snprintf(sline,sizeof sline,"%s:!:%ld:0:99999:7:::",name,days);
    snprintf(gline,sizeof gline,"%s:x:%u:",name,gid);
    snprintf(gsline,sizeof gsline,"%s:!::",name);

    char *np=NULL,*ns=NULL,*ng=NULL,*ngs=NULL;
    if(!account_append_line(passwd_text,pline,&np) ||
       !account_append_line(shadow_text,sline,&ns)) {
        fprintf(stderr,"useradd: out of memory\n");
        free(np);free(ns); goto fail;
    }

    if(!gid_set && !account_has_name(group_text,name)) {
        if(!account_append_line(group_text,gline,&ng) ||
           !account_append_line(gshadow_text,gsline,&ngs)) {
            fprintf(stderr,"useradd: out of memory\n");
            free(np);free(ns);free(ng);free(ngs); goto fail;
        }
    } else {
        ng=(char*)malloc(strlen(group_text)+1);
        ngs=(char*)malloc(strlen(gshadow_text)+1);
        if(!ng||!ngs){free(np);free(ns);free(ng);free(ngs);goto fail;}
        strcpy(ng,group_text); strcpy(ngs,gshadow_text);
    }

    if(write_account_file(root,"etc/passwd",np,0644)!=0 ||
       write_account_file(root,"etc/shadow",ns,0640)!=0 ||
       write_account_file(root,"etc/group",ng,0644)!=0 ||
       write_account_file(root,"etc/gshadow",ngs,0640)!=0) {
        free(np);free(ns);free(ng);free(ngs); goto fail;
    }
    free(np);free(ns);free(ng);free(ngs);

    if(create_home) {
        char actual_home[512];
        if(!account_join_path(actual_home,sizeof actual_home,root,home)) {
            fprintf(stderr,"useradd: home path too long\n"); goto fail;
        }
        if(!account_vfs_mkdir_p(actual_home,0755)) {
            fprintf(stderr,"useradd: cannot create home '%s'\n",actual_home); goto fail;
        }
        if(vfs_chown(actual_home,uid,gid)!=0) {
            fprintf(stderr,"useradd: cannot set ownership on '%s'\n",actual_home); goto fail;
        }
    }

    app_printf("useradd: created %s uid=%u gid=%u home=%s shell=%s%s\n",
               name,uid,gid,home,shell,create_home?"":" (no home)");
    free(passwd_text);free(shadow_text);free(group_text);free(gshadow_text);
    return 0;

fail:
    free(passwd_text);free(shadow_text);free(group_text);free(gshadow_text);
    return 1;
}
