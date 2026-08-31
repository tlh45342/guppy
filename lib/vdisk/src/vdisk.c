#define _FILE_OFFSET_BITS 64
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "vdisk_internal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/types.h>
#endif

#ifdef _WIN32
#define host_seek _fseeki64
#define host_tell _ftelli64
#else
#define host_seek fseeko
#define host_tell ftello
#endif

static char *dupstr(const char *s) {
    size_t n; char *p;
    if (!s) return NULL;
    n=strlen(s)+1; p=(char*)malloc(n);
    if (p) memcpy(p,s,n);
    return p;
}
int vdisk_host_seek(FILE *fp,uint64_t off) {
#ifdef _WIN32
    return host_seek(fp, (__int64)off, SEEK_SET)==0?0:-1;
#else
    return host_seek(fp, (off_t)off, SEEK_SET)==0?0:-1;
#endif
}
int vdisk_host_read(FILE *fp,uint64_t off,void *buf,size_t len) {
    if (vdisk_host_seek(fp,off)!=0) return -1;
    return fread(buf,1,len,fp)==len?0:-1;
}
int vdisk_host_write(FILE *fp,uint64_t off,const void *buf,size_t len) {
    if (vdisk_host_seek(fp,off)!=0) return -1;
    return fwrite(buf,1,len,fp)==len?0:-1;
}
int vdisk_host_get_size(FILE *fp,uint64_t *out) {
#ifdef _WIN32
    __int64 cur,end;
#else
    off_t cur,end;
#endif
    if (!fp||!out) return -1;
    cur=host_tell(fp); if(cur<0) return -1;
    if(host_seek(fp,0,SEEK_END)!=0) return -1;
    end=host_tell(fp);
    if(host_seek(fp,cur,SEEK_SET)!=0 || end<0) return -1;
    *out=(uint64_t)end; return 0;
}

static int ext_eq(const char *a,const char *b) {
    while(*a&&*b) {
        if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a==0&&*b==0;
}
vdisk_format_t vdisk_format_from_extension(const char *path) {
    const char *dot = path ? strrchr(path,'.') : NULL;
    if(!dot) return VDISK_FMT_AUTO;
    if(ext_eq(dot,".vdi")) return VDISK_FMT_VDI;
    if(ext_eq(dot,".vmdk")) return VDISK_FMT_VMDK;
    if(ext_eq(dot,".vhd")) return VDISK_FMT_VHD;
    if(ext_eq(dot,".qcow2")||ext_eq(dot,".qcow")) return VDISK_FMT_QCOW2;
    if(ext_eq(dot,".raw")||ext_eq(dot,".img")||ext_eq(dot,".hdd"))
        return VDISK_FMT_RAW;
    return VDISK_FMT_AUTO;
}
const char *vdisk_format_name(vdisk_format_t f) {
    switch(f) {
        case VDISK_FMT_RAW:return "raw";
        case VDISK_FMT_VDI:return "vdi";
        case VDISK_FMT_VMDK:return "vmdk";
        case VDISK_FMT_VHD:return "vhd";
        case VDISK_FMT_QCOW2:return "qcow2";
        default:return "auto";
    }
}
static const vdisk_driver_t *driver_for(vdisk_format_t f) {
    switch(f) {
        case VDISK_FMT_RAW:return &vdisk_raw_driver;
        case VDISK_FMT_VDI:return &vdisk_vdi_driver;
        default:return NULL;
    }
}
static int open_file(vdisk_t *d) {
    d->fp=fopen(d->path,"rb+");
    if(d->fp){d->writable=true;return 0;}
    d->fp=fopen(d->path,"rb");
    if(d->fp){d->writable=false;return 0;}
    return -1;
}
int vdisk_open(const char *path,vdisk_format_t format,vdisk_t **out) {
    vdisk_t *d; const vdisk_driver_t *drv;
    vdisk_format_t hint;
    if(!path||!out) return -1;
    *out=NULL;
    d=(vdisk_t*)calloc(1,sizeof(*d)); if(!d)return -1;
    d->path=dupstr(path); if(!d->path){free(d);return -1;}
    if(open_file(d)!=0){vdisk_close(d);return -1;}

    hint=vdisk_format_from_extension(path);
    if(format==VDISK_FMT_AUTO) {
        /* Recognized extensions choose a backend, but the backend must probe.
           Unknown extensions preserve Guppy's historical RAW behavior. */
        format=(hint==VDISK_FMT_AUTO)?VDISK_FMT_RAW:hint;
    }
    drv=driver_for(format);
    if(!drv || !drv->probe(d->fp)){vdisk_close(d);return -1;}
    d->driver=drv; d->format=format;
    if(drv->open(d)!=0){vdisk_close(d);return -1;}
    *out=d; return 0;
}
int vdisk_create(const char *path,vdisk_format_t format,uint64_t n,vdisk_t **out) {
    vdisk_t *d; const vdisk_driver_t *drv;
    if(!path||!out||!n) return -1;
    *out=NULL;
    if(format==VDISK_FMT_AUTO) {
        format=vdisk_format_from_extension(path);
        if(format==VDISK_FMT_AUTO)format=VDISK_FMT_RAW;
    }
    drv=driver_for(format); if(!drv)return -1;
    d=(vdisk_t*)calloc(1,sizeof(*d)); if(!d)return -1;
    d->path=dupstr(path); d->driver=drv; d->format=format; d->writable=true;
    if(!d->path){free(d);return -1;}
    d->fp=fopen(path,"wb+"); if(!d->fp){vdisk_close(d);return -1;}
    if(drv->create(d,n)!=0){vdisk_close(d);return -1;}
    *out=d; return 0;
}
void vdisk_close(vdisk_t*d) {
    if(!d)return;
    if(d->driver&&d->driver->close)d->driver->close(d);
    if(d->fp)fclose(d->fp);
    free(d->path); free(d);
}
int vdisk_read(vdisk_t*d,uint64_t o,void*b,size_t n) {
    return d&&d->driver&&d->driver->read?d->driver->read(d,o,b,n):-1;
}
int vdisk_write(vdisk_t*d,uint64_t o,const void*b,size_t n) {
    return d&&d->driver&&d->driver->write?d->driver->write(d,o,b,n):-1;
}
int vdisk_flush(vdisk_t*d) {
    return d&&d->driver&&d->driver->flush?d->driver->flush(d):-1;
}
uint64_t vdisk_get_size(const vdisk_t*d){return d?d->size_bytes:0;}
bool vdisk_is_writable(const vdisk_t*d){return d?d->writable:false;}
vdisk_format_t vdisk_get_format(const vdisk_t*d){return d?d->format:VDISK_FMT_AUTO;}
const char *vdisk_get_path(const vdisk_t*d){return d?d->path:NULL;}
