#include "cmd.h"
#include "use.h"
#include <stdio.h>
#include <string.h>

int cmd_use(int argc, char **argv){
    if (argc >= 2 && strcmp(argv[1],"add")==0){
        const char *img=NULL; const char *as=NULL; int ro=0;
        for(int i=2;i<argc;i++){
            if(strcmp(argv[i],"-i")==0 && i+1<argc) img=argv[++i];
            else if(strcmp(argv[i],"-as")==0 && i+1<argc) as=argv[++i];
            else if(strcmp(argv[i],"--ro")==0) ro=1;
        }
        if(!img||!as||strlen(as)!=1){ fprintf(stderr,"use add -i <img> -as <a..z> [--ro]\n"); return 2; }
        if(!use_add(as[0],img,ro)) { fprintf(stderr,"use: failed\n"); return 1; }
        printf("Mapped /dev/%c -> %s%s\n", as[0], img, ro?" [ro]":"");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1],"select")==0){
        if(argc<3){ fprintf(stderr,"use select <a..z>\n"); return 2; }
        if(!use_select(argv[2][0])){ fprintf(stderr,"use: no such device\n"); return 1; }
        printf("Selected /dev/%c\n", argv[2][0]); return 0;
    }
    if (argc >= 2 && strcmp(argv[1],"ls")==0){ use_list(); return 0; }
    if (argc >= 2 && strcmp(argv[1],"rm")==0){
        if(argc<3){ fprintf(stderr,"use rm <a..z>\n"); return 2; }
        if(!use_rm(argv[2][0])){ fprintf(stderr,"use: no such device\n"); return 1; }
        return 0;
    }
    fprintf(stderr, "use add -i <img> -as a | use select a | use ls | use rm a\n");
    return 2;
}