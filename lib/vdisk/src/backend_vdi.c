#include "vdisk_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VDI_SIGNATURE          0xBEDA107FUL
#define VDI_VERSION_1_1       0x00010001UL
#define VDI_IMAGE_TYPE_FIXED  2U
#define VDI_SECTOR_SIZE       512U
#define VDI_BLOCK_SIZE        1048576U
#define VDI_HEADER_BYTES      512U

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const unsigned char *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static void put32(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static void put64(unsigned char *p, uint64_t v) {
    put32(p,(uint32_t)v); put32(p+4,(uint32_t)(v>>32));
}
static uint64_t round_up_u64(uint64_t n, uint64_t a) {
    return ((n + a - 1) / a) * a;
}
static int read_header(FILE *fp, unsigned char h[VDI_HEADER_BYTES]) {
    return vdisk_host_read(fp,0,h,VDI_HEADER_BYTES);
}

/* UUID bytes need only be unique for a base image.  Keep libvdisk dependency-free. */
static void make_uuid(unsigned char u[16]) {
    static unsigned counter;
    unsigned i;
    uint64_t x = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)u ^ ++counter;
    for (i=0;i<16;i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        u[i]=(unsigned char)(x >> ((i & 7U) * 8U));
    }
    u[6]=(unsigned char)((u[6]&0x0fU)|0x40U);
    u[8]=(unsigned char)((u[8]&0x3fU)|0x80U);
}

static int vdi_probe(FILE *fp) {
    unsigned char h[VDI_HEADER_BYTES];
    return read_header(fp,h)==0 && le32(h+0x40)==VDI_SIGNATURE;
}

static int vdi_open_fixed(vdisk_t *d) {
    unsigned char h[VDI_HEADER_BYTES];
    uint32_t sig,version,hdrsz,type,off_blocks,off_data,sector_size;
    uint32_t block_size,block_extra,blocks,allocated;
    uint64_t disk_size,host_size,required;

    if (read_header(d->fp,h)!=0 || vdisk_host_get_size(d->fp,&host_size)!=0) return -1;
    sig=le32(h+0x40); version=le32(h+0x44); hdrsz=le32(h+0x48);
    type=le32(h+0x4c); off_blocks=le32(h+0x154); off_data=le32(h+0x158);
    sector_size=le32(h+0x168); disk_size=le64(h+0x170);
    block_size=le32(h+0x178); block_extra=le32(h+0x17c);
    blocks=le32(h+0x180); allocated=le32(h+0x184);

    if(sig!=VDI_SIGNATURE || version!=VDI_VERSION_1_1 || hdrsz<0x180U ||
       type!=VDI_IMAGE_TYPE_FIXED || sector_size!=VDI_SECTOR_SIZE ||
       !disk_size || !block_size || !blocks || allocated!=blocks ||
       !off_blocks || !off_data || off_data<=off_blocks || block_extra!=0) return -1;
    if(disk_size>(uint64_t)blocks*block_size || disk_size>UINT64_MAX-off_data) return -1;
    required=(uint64_t)off_data+disk_size;
    if(host_size<required) return -1;

    d->size_bytes=disk_size; d->host_size_bytes=host_size;
    d->data_offset=off_data; d->block_size=block_size; d->block_extra=0;
    return 0;
}

static int vdi_create_fixed(vdisk_t *d, uint64_t size_bytes) {
    unsigned char h[VDI_HEADER_BYTES];
    unsigned char uuid[16];
    unsigned char zero[4096];
    uint32_t blocks, off_blocks, off_data;
    uint64_t map_bytes, map_padded, host_size, pos;
    uint32_t i;

    if(!d || !d->fp || !size_bytes) return -1;
    if(size_bytes > UINT64_MAX-(VDI_SECTOR_SIZE-1)) return -1;
    size_bytes=round_up_u64(size_bytes,VDI_SECTOR_SIZE);
    blocks=(uint32_t)((size_bytes+VDI_BLOCK_SIZE-1)/VDI_BLOCK_SIZE);
    if(!blocks) return -1;

    map_bytes=(uint64_t)blocks*4U;
    map_padded=round_up_u64(map_bytes,VDI_SECTOR_SIZE);
    if(map_padded>UINT32_MAX-VDI_HEADER_BYTES) return -1;
    off_blocks=VDI_HEADER_BYTES;
    off_data=(uint32_t)(VDI_HEADER_BYTES+map_padded);
    if(size_bytes>UINT64_MAX-off_data) return -1;
    host_size=(uint64_t)off_data+size_bytes;

    memset(h,0,sizeof(h));
    memcpy(h,"<<< Guppy VirtualBox Disk Image >>>\n",35);
    put32(h+0x40,VDI_SIGNATURE); put32(h+0x44,VDI_VERSION_1_1);
    put32(h+0x48,0x180U); put32(h+0x4c,VDI_IMAGE_TYPE_FIXED);
    memcpy(h+0x54,"Guppy fixed VDI",15);
    put32(h+0x154,off_blocks); put32(h+0x158,off_data);
    put32(h+0x168,VDI_SECTOR_SIZE); put64(h+0x170,size_bytes);
    put32(h+0x178,VDI_BLOCK_SIZE); put32(h+0x17c,0);
    put32(h+0x180,blocks); put32(h+0x184,blocks);
    make_uuid(uuid); memcpy(h+0x188,uuid,16);
    make_uuid(uuid); memcpy(h+0x198,uuid,16);

    if(vdisk_host_write(d->fp,0,h,sizeof(h))!=0) return -1;
    for(i=0;i<blocks;i++) {
        unsigned char e[4]; put32(e,i);
        if(vdisk_host_write(d->fp,(uint64_t)off_blocks+(uint64_t)i*4U,e,4)!=0) return -1;
    }
    memset(zero,0,sizeof(zero));
    pos=(uint64_t)off_blocks+map_bytes;
    while(pos<off_data) {
        size_t n=(size_t)(((uint64_t)off_data-pos)>sizeof(zero)?sizeof(zero):((uint64_t)off_data-pos));
        if(vdisk_host_write(d->fp,pos,zero,n)!=0) return -1;
        pos+=n;
    }
    if(vdisk_host_seek(d->fp,host_size-1)!=0 || fputc(0,d->fp)==EOF || fflush(d->fp)!=0) return -1;

    d->size_bytes=size_bytes; d->host_size_bytes=host_size;
    d->data_offset=off_data; d->block_size=VDI_BLOCK_SIZE; d->block_extra=0;
    return 0;
}

static int bounds_ok(const vdisk_t*d,uint64_t off,size_t len) {
    return off<=d->size_bytes && (uint64_t)len<=d->size_bytes-off;
}
static int vdi_read_fixed(vdisk_t*d,uint64_t off,void*buf,size_t len) {
    return bounds_ok(d,off,len)?vdisk_host_read(d->fp,d->data_offset+off,buf,len):-1;
}
static int vdi_write_fixed(vdisk_t*d,uint64_t off,const void*buf,size_t len) {
    return d->writable&&bounds_ok(d,off,len)?vdisk_host_write(d->fp,d->data_offset+off,buf,len):-1;
}
static int vdi_flush(vdisk_t*d){return fflush(d->fp)==0?0:-1;}
static void vdi_close(vdisk_t*d){(void)d;}

const vdisk_driver_t vdisk_vdi_driver={
    VDISK_FMT_VDI,"vdi",vdi_probe,vdi_open_fixed,vdi_create_fixed,
    vdi_read_fixed,vdi_write_fixed,vdi_flush,vdi_close
};
