#define _FILE_OFFSET_BITS 64
#include "vdisk_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int path_parts(const char *path, char *dir, size_t dsz, char *stem, size_t ssz) {
    const char *slash1=strrchr(path,'/'), *slash2=strrchr(path,'\\');
    const char *slash=slash1;
    const char *base;
    const char *dot;
    size_t dn=0,bn;
    if(slash2 && (!slash || slash2>slash)) slash=slash2;
    base=slash?slash+1:path;
    if(slash){dn=(size_t)(slash-path)+1; if(dn>=dsz)return -1; memcpy(dir,path,dn);dir[dn]=0;}
    else { if(dsz<1)return -1; dir[0]=0; }
    dot=strrchr(base,'.');
    bn=dot && strcmp(dot,".vmdk")==0 ? (size_t)(dot-base) : strlen(base);
    if(bn+1>ssz)return -1;
    memcpy(stem,base,bn);stem[bn]=0;
    return 0;
}

static int extent_path_for(const char *descriptor, const char *extent_name, char *out, size_t outsz) {
    char dir[1024],stem[512];
    if(path_parts(descriptor,dir,sizeof dir,stem,sizeof stem)!=0)return -1;
    if(snprintf(out,outsz,"%s%s",dir,extent_name)>= (int)outsz)return -1;
    return 0;
}

static int make_flat_name(const char *descriptor, char *name, size_t nsz, char *path, size_t psz) {
    char dir[1024],stem[512];
    if(path_parts(descriptor,dir,sizeof dir,stem,sizeof stem)!=0)return -1;
    if(snprintf(name,nsz,"%s-flat.vmdk",stem)>=(int)nsz)return -1;
    if(snprintf(path,psz,"%s%s",dir,name)>=(int)psz)return -1;
    return 0;
}

static int parse_extent_line(const char *line, uint64_t *sectors, char *extent, size_t esz) {
    unsigned long long sec=0;
    char mode[32],file[512];
    const char *p=line;
    while(*p && isspace((unsigned char)*p))p++;
    if(strncmp(p,"RW ",3)!=0)return -1;
    if(sscanf(p,"RW %llu %31s \"%511[^\"]\"",&sec,mode,file)!=3)return -1;
    if(strcmp(mode,"VMFS")!=0 && strcmp(mode,"FLAT")!=0)return -1;
    if(sec==0 || strlen(file)+1>esz)return -1;
    *sectors=(uint64_t)sec;
    strcpy(extent,file);
    return 0;
}

static int read_descriptor(FILE *fp, uint64_t *sectors, char *extent, size_t esz, char *ctype, size_t csz) {
    char line[1024];
    int got_extent=0;
    if(vdisk_host_seek(fp,0)!=0)return -1;
    while(fgets(line,sizeof line,fp)) {
        char *p=line;
        while(*p && isspace((unsigned char)*p))p++;
        if(!got_extent && !strncmp(p,"RW ",3)) {
            if(parse_extent_line(p,sectors,extent,esz)==0)got_extent=1;
        }
        if(!strncmp(p,"createType=",11)) {
            char *q=strchr(p,'\"'), *r=q?strchr(q+1,'\"'):NULL;
            if(q&&r&&r>q+1) { size_t n=(size_t)(r-q-1); if(n>=csz)n=csz-1; memcpy(ctype,q+1,n);ctype[n]=0; }
        }
    }
    return got_extent?0:-1;
}

static int vmdk_probe(FILE *fp) {
    unsigned char h[20];
    if(vdisk_host_read(fp,0,h,sizeof h)!=0)return 0;
    return memcmp(h,"# Disk DescriptorFile",20)==0;
}

static int vmdk_open(vdisk_t *d) {
    char extent_name[512], extent_path[1536], ctype[64]={0};
    uint64_t sectors=0,host=0;
    FILE *efp;
    if(read_descriptor(d->fp,&sectors,extent_name,sizeof extent_name,ctype,sizeof ctype)!=0)return -1;
    if(strcmp(ctype,"vmfs")!=0 && strcmp(ctype,"monolithicFlat")!=0)return -1;
    if(extent_path_for(d->path,extent_name,extent_path,sizeof extent_path)!=0)return -1;
    efp=fopen(extent_path,d->writable?"rb+":"rb");
    if(!efp && d->writable){efp=fopen(extent_path,"rb");d->writable=false;}
    if(!efp)return -1;
    if(vdisk_host_get_size(efp,&host)!=0 || host<sectors*512ULL){fclose(efp);return -1;}
    fclose(d->fp);d->fp=efp;
    d->size_bytes=sectors*512ULL;
    d->host_size_bytes=host;
    d->data_offset=0;
    d->image_type=1;
    return 0;
}

static int size_host_file(FILE *fp,uint64_t size_bytes) {
    unsigned char z=0;
    if(size_bytes==0)return -1;
    if(vdisk_host_write(fp,size_bytes-1,&z,1)!=0)return -1;
    return fflush(fp)==0?0:-1;
}

static int vmdk_create(vdisk_t *d,uint64_t size_bytes) {
    char extent_name[512],extent_path[1536];
    FILE *efp;
    uint64_t sectors;
    unsigned cyl;
    if((d->create_flags & VDISK_CREATE_VMDK_LAYOUT_MASK)!=VDISK_CREATE_VMDK_FLAT)return -1;
    if(size_bytes%512ULL)size_bytes=(size_bytes+511ULL)&~511ULL;
    sectors=size_bytes/512ULL;
    if(make_flat_name(d->path,extent_name,sizeof extent_name,extent_path,sizeof extent_path)!=0)return -1;
    cyl=(unsigned)(sectors/(255ULL*63ULL)); if(cyl==0)cyl=1;
    if(fprintf(d->fp,
        "# Disk DescriptorFile\n"
        "version=1\n"
        "encoding=\"UTF-8\"\n"
        "CID=fffffffe\n"
        "parentCID=ffffffff\n"
        "createType=\"vmfs\"\n\n"
        "# Extent description\n"
        "RW %llu VMFS \"%s\"\n\n"
        "# The Disk Data Base\n"
        "#DDB\n"
        "ddb.adapterType = \"lsilogic\"\n"
        "ddb.geometry.cylinders = \"%u\"\n"
        "ddb.geometry.heads = \"255\"\n"
        "ddb.geometry.sectors = \"63\"\n",
        (unsigned long long)sectors,extent_name,cyl)<0 || fflush(d->fp)!=0)return -1;
    efp=fopen(extent_path,"wb+"); if(!efp)return -1;
    if(size_host_file(efp,size_bytes)!=0){fclose(efp);remove(extent_path);return -1;}
    fclose(d->fp);d->fp=efp;
    d->size_bytes=size_bytes;d->host_size_bytes=size_bytes;d->data_offset=0;d->image_type=1;
    return 0;
}

static int vmdk_read(vdisk_t*d,uint64_t o,void*b,size_t n) {
    if(o>d->size_bytes || (uint64_t)n>d->size_bytes-o)return -1;
    return vdisk_host_read(d->fp,o,b,n);
}
static int vmdk_write(vdisk_t*d,uint64_t o,const void*b,size_t n) {
    if(!d->writable || o>d->size_bytes || (uint64_t)n>d->size_bytes-o)return -1;
    return vdisk_host_write(d->fp,o,b,n);
}
static int vmdk_flush(vdisk_t*d){return fflush(d->fp)==0?0:-1;}
static void vmdk_close(vdisk_t*d){(void)d;}

int vdisk_vmdk_inspect(const char *path, FILE *fp, vdisk_info_t *info) {
    char extent_name[512],extent_path[1536],ctype[64]={0};
    uint64_t sectors=0,esz=0;
    FILE *efp=NULL;
    if(read_descriptor(fp,&sectors,extent_name,sizeof extent_name,ctype,sizeof ctype)!=0)return -1;
    info->format=VDISK_FMT_VMDK;
    info->sector_size=512;
    info->logical_size_bytes=sectors*512ULL;
    info->image_type=1;
    if(strcmp(ctype,"vmfs")!=0 && strcmp(ctype,"monolithicFlat")!=0){
        info->supported=false;snprintf(info->variant,sizeof(info->variant),"%.31s",ctype[0]?ctype:"descriptor");info->checks_passed=1;return 0;
    }
    if(extent_path_for(path,extent_name,extent_path,sizeof extent_path)!=0)return -1;
    efp=fopen(extent_path,"rb");
    if(!efp){info->supported=true;strcpy(info->variant,"flat/external");info->checks_failed=1;return 0;}
    if(vdisk_host_get_size(efp,&esz)!=0){fclose(efp);return -1;} fclose(efp);
    info->supported=true;
    strcpy(info->variant,"flat/external");
    if(esz>=info->logical_size_bytes)info->checks_passed=3;else info->checks_failed=1;
    info->structurally_valid=(info->checks_failed==0);
    return 0;
}

const vdisk_driver_t vdisk_vmdk_driver={
    VDISK_FMT_VMDK,"vmdk",vmdk_probe,vmdk_open,vmdk_create,NULL,
    vmdk_read,vmdk_write,vmdk_flush,vmdk_close
};
