// src/cmd_passwd.c — non-interactive offline shadow password update.
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
    app_printf("usage: passwd -R <root> <name> <password>\n"
               "       passwd -R <root> --hash <crypt-hash> <name>\n");
}

static void make_salt(char out[17], const char *name)
{
    static const char a[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    uint64_t x=(uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)name;
    for(size_t i=0;i<16;i++) {
        x ^= x<<13; x ^= x>>7; x ^= x<<17;
        out[i]=a[x&63u];
    }
    out[16]='\0';
}

int cmd_passwd(int argc, char **argv)
{
    const char *root=NULL,*name=NULL,*password=NULL,*hash_arg=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"-R") && i+1<argc) root=argv[++i];
        else if(!strcmp(argv[i],"--hash") && i+1<argc) hash_arg=argv[++i];
        else if(!name) name=argv[i];
        else if(!password) password=argv[i];
        else {usage();return 2;}
    }
    if(!root||!name||(!hash_arg&&!password) || (hash_arg&&password)) {usage();return 2;}
    if(!account_valid_name(name)){fprintf(stderr,"passwd: invalid user name '%s'\n",name);return 2;}

    char ppath[512],spath[512];
    if(!account_join_path(ppath,sizeof ppath,root,"etc/passwd") ||
       !account_join_path(spath,sizeof spath,root,"etc/shadow")) return 1;

    char *pt=NULL,*st=NULL;
    if(!account_vfs_read_all(ppath,&pt,NULL) || !account_vfs_read_all(spath,&st,NULL)) {
        fprintf(stderr,"passwd: cannot read account database\n");
        free(pt);free(st);return 1;
    }
    if(!account_has_name(pt,name)) {
        fprintf(stderr,"passwd: user '%s' does not exist\n",name);
        free(pt);free(st);return 1;
    }

    char hash[160];
    if(hash_arg) {
        if(strlen(hash_arg)>=sizeof hash || hash_arg[0]!='$') {
            fprintf(stderr,"passwd: --hash expects a crypt-style hash\n");
            free(pt);free(st);return 2;
        }
        snprintf(hash,sizeof hash,"%s",hash_arg);
    } else {
        char salt[17];
        make_salt(salt,name);
        if(!account_sha256_crypt(password,salt,hash,sizeof hash)) {
            fprintf(stderr,"passwd: password hashing failed\n");
            free(pt);free(st);return 1;
        }
    }

    long days=(long)(time(NULL)/86400);
    char line[320];
    snprintf(line,sizeof line,"%s:%s:%ld:0:99999:7:::",name,hash,days);

    char *ns=NULL;
    if(!account_replace_named_line(st,name,line,&ns)) {
        fprintf(stderr,"passwd: cannot update shadow entry\n");
        free(pt);free(st);return 1;
    }
    if(!account_vfs_write_all(spath,ns,strlen(ns),0640)) {
        fprintf(stderr,"passwd: cannot write '%s'\n",spath);
        free(pt);free(st);free(ns);return 1;
    }
    (void)vfs_chmod(spath,0640);

    app_printf("passwd: updated password for %s\n",name);
    free(pt);free(st);free(ns);
    return 0;
}
