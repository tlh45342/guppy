#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "app_printf.h"
#include "vdisk.h"

extern uint64_t parse_size(const char *s, int *ok);

static bool starts_with(const char *s,const char *prefix) {
    size_t ls,lp; if(!s||!prefix)return false; ls=strlen(s);lp=strlen(prefix);
    return lp<=ls && strncmp(s,prefix,lp)==0;
}
static uint64_t parse_size_arg_inline(const char *arg) {
    const char *eq=strchr(arg,'='); int ok=0;
    uint64_t v; if(!eq||!eq[1])return 0; v=parse_size(eq+1,&ok); return ok?v:0;
}

static unsigned parse_vmdk_type(const char *s) {
    if(!s)return 0;
    if(strcmp(s,"flat")==0)return VDISK_CREATE_VMDK_FLAT;
    if(strcmp(s,"monolithic-flat")==0)return VDISK_CREATE_VMDK_MONOLITHIC_FLAT;
    if(strcmp(s,"monolithic-sparse")==0)return VDISK_CREATE_VMDK_MONOLITHIC_SPARSE;
    if(strcmp(s,"split-flat")==0)return VDISK_CREATE_VMDK_SPLIT_FLAT;
    if(strcmp(s,"split-sparse")==0)return VDISK_CREATE_VMDK_SPLIT_SPARSE;
    return 0;
}
static const char *vmdk_type_name(unsigned f) {
    switch(f & VDISK_CREATE_VMDK_LAYOUT_MASK) {
        case VDISK_CREATE_VMDK_FLAT:return "flat";
        case VDISK_CREATE_VMDK_MONOLITHIC_FLAT:return "monolithic-flat";
        case VDISK_CREATE_VMDK_MONOLITHIC_SPARSE:return "monolithic-sparse";
        case VDISK_CREATE_VMDK_SPLIT_FLAT:return "split-flat";
        case VDISK_CREATE_VMDK_SPLIT_SPARSE:return "split-sparse";
        default:return NULL;
    }
}

static vdisk_format_t parse_format(const char *s) {
    if(!s)return VDISK_FMT_AUTO;
    if(strcmp(s,"raw")==0)return VDISK_FMT_RAW;
    if(strcmp(s,"vdi")==0)return VDISK_FMT_VDI;
    if(strcmp(s,"vmdk")==0)return VDISK_FMT_VMDK;
    if(strcmp(s,"vhd")==0)return VDISK_FMT_VHD;
    if(strcmp(s,"qcow2")==0||strcmp(s,"qcow")==0)return VDISK_FMT_QCOW2;
    return VDISK_FMT_AUTO;
}

int cmd_create(int argc,char **argv) {
    const char *img=NULL; uint64_t size_bytes=0; bool use_mbr=false, sparse=false;
    unsigned vmdk_type=0, create_flags=0;
    vdisk_format_t format=VDISK_FMT_AUTO; vdisk_t *disk=NULL;
    int i;
    if(argc<3) {
        fprintf(stderr,"usage: create [-f raw|vdi|vhd|vmdk] <img> <size> [options]\n");
        fprintf(stderr,"       VMDK: --vmdk-type flat|monolithic-flat|monolithic-sparse|split-flat|split-sparse\n");
        return 2;
    }
    for(i=1;i<argc;i++) {
        if(strcmp(argv[i],"-f")==0||strcmp(argv[i],"--format")==0) {
            if(i+1>=argc){fprintf(stderr,"create: %s requires a format\n",argv[i]);return 2;}
            format=parse_format(argv[++i]);
            if(format==VDISK_FMT_AUTO){fprintf(stderr,"create: unsupported format '%s'\n",argv[i]);return 2;}
        } else if(starts_with(argv[i],"--format=")) {
            format=parse_format(strchr(argv[i],'=')+1);
            if(format==VDISK_FMT_AUTO){fprintf(stderr,"create: unsupported format '%s'\n",strchr(argv[i],'=')+1);return 2;}
        } else if(strcmp(argv[i],"--vmdk-type")==0) {
            if(i+1>=argc){fprintf(stderr,"create: --vmdk-type requires a value\n");return 2;}
            vmdk_type=parse_vmdk_type(argv[++i]);
            if(!vmdk_type){fprintf(stderr,"create: unsupported VMDK type '%s'\n",argv[i]);return 2;}
        } else if(starts_with(argv[i],"--vmdk-type=")) {
            const char *v=strchr(argv[i],'=')+1; vmdk_type=parse_vmdk_type(v);
            if(!vmdk_type){fprintf(stderr,"create: unsupported VMDK type '%s'\n",v);return 2;}
        } else if(strcmp(argv[i],"--size")==0) {
            int ok=0; if(i+1>=argc){fprintf(stderr,"create: --size requires a value\n");return 2;}
            size_bytes=parse_size(argv[++i],&ok); if(!ok||!size_bytes){fprintf(stderr,"create: invalid --size value\n");return 2;}
        } else if(starts_with(argv[i],"--size=")) {
            size_bytes=parse_size_arg_inline(argv[i]); if(!size_bytes){fprintf(stderr,"create: invalid --size value\n");return 2;}
        } else if(strcmp(argv[i],"--mbr")==0) use_mbr=true;
        else if(strcmp(argv[i],"--sparse")==0) sparse=true;
        else if(argv[i][0]=='-'){fprintf(stderr,"create: unknown option: %s\n",argv[i]);return 2;}
        else if(!img) img=argv[i];
        else if(!size_bytes) { int ok=0; size_bytes=parse_size(argv[i],&ok); if(!ok||!size_bytes){fprintf(stderr,"create: invalid size value '%s'\n",argv[i]);return 2;} }
        else {fprintf(stderr,"create: extra argument: %s\n",argv[i]);return 2;}
    }
    if(!img||!size_bytes){fprintf(stderr,"create: image and size are required\n");return 2;}
    if(format==VDISK_FMT_AUTO){format=vdisk_format_from_extension(img);if(format==VDISK_FMT_AUTO)format=VDISK_FMT_RAW;}
    if(format!=VDISK_FMT_RAW&&format!=VDISK_FMT_VDI&&format!=VDISK_FMT_VHD&&format!=VDISK_FMT_VMDK){fprintf(stderr,"create: format '%s' is not implemented yet\n",vdisk_format_name(format));return 2;}
    if(vmdk_type && format!=VDISK_FMT_VMDK){fprintf(stderr,"create: --vmdk-type requires VMDK format\n");return 2;}
    if(format==VDISK_FMT_VMDK && !vmdk_type)vmdk_type=VDISK_CREATE_VMDK_FLAT;
    if(format==VDISK_FMT_VMDK && sparse){fprintf(stderr,"create: use --vmdk-type monolithic-sparse or split-sparse for sparse VMDK layouts\n");return 2;}
    if(format==VDISK_FMT_VMDK && vmdk_type!=VDISK_CREATE_VMDK_FLAT){fprintf(stderr,"create: VMDK type '%s' is recognized but not implemented yet\n",vmdk_type_name(vmdk_type));return 2;}
    if(sparse && format!=VDISK_FMT_VDI){fprintf(stderr,"create: --sparse is currently supported only for VDI\n");return 2;}
    create_flags=(sparse?VDISK_CREATE_SPARSE:0)|vmdk_type;
    if(vdisk_create_ex(img,format,size_bytes,create_flags,&disk)!=0){fprintf(stderr,"create: failed to create %s image '%s'\n",vdisk_format_name(format),img);return 1;}
    if(use_mbr) {
        unsigned char mbr[512]={0}; mbr[510]=0x55;mbr[511]=0xaa;
        if(vdisk_write(disk,0,mbr,sizeof(mbr))!=0||vdisk_flush(disk)!=0){fprintf(stderr,"create: failed to write blank MBR\n");vdisk_close(disk);return 1;}
    }
    app_printf("Created %s (%llu bytes)%s%s%s\n", img,
           (unsigned long long)vdisk_get_size(disk),
           format==VDISK_FMT_RAW ? "" : " [",
           format==VDISK_FMT_RAW ? "" : vdisk_format_name(format),
           format==VDISK_FMT_RAW ? (use_mbr ? " with MBR" : "") :
           (format==VDISK_FMT_VMDK ? ", flat]" :
            (use_mbr ? (sparse ? ", sparse, MBR]" : ", MBR]") : (sparse ? ", sparse]" : "]"))));
    vdisk_close(disk); return 0;
}
