#include "vdisk_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VDI_SIGNATURE            0xBEDA107FUL
#define VDI_VERSION_1_1         0x00010001UL
#define VDI_IMAGE_TYPE_DYNAMIC  1U
#define VDI_IMAGE_TYPE_FIXED    2U
#define VDI_SECTOR_SIZE         512U
#define VDI_BLOCK_SIZE          1048576U
#define VDI_HEADER_BYTES        512U
#define VDI_BLOCK_UNALLOCATED   0xFFFFFFFFU

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

/* UUID bytes need only be unique for a base image. Keep libvdisk dependency-free. */
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

static int vdi_open(vdisk_t *d) {
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
       (type!=VDI_IMAGE_TYPE_FIXED && type!=VDI_IMAGE_TYPE_DYNAMIC) ||
       sector_size!=VDI_SECTOR_SIZE || !disk_size || !block_size || !blocks ||
       !off_blocks || !off_data || off_data<=off_blocks || block_extra!=0 ||
       allocated>blocks) return -1;

    if(disk_size>(uint64_t)blocks*block_size) return -1;

    if(type==VDI_IMAGE_TYPE_FIXED) {
        if(allocated!=blocks || disk_size>UINT64_MAX-off_data) return -1;
        required=(uint64_t)off_data+disk_size;
    } else {
        if((uint64_t)allocated > (UINT64_MAX-off_data)/block_size) return -1;
        required=(uint64_t)off_data+(uint64_t)allocated*block_size;
    }
    if(host_size<required) return -1;

    d->size_bytes=disk_size;
    d->host_size_bytes=host_size;
    d->data_offset=off_data;
    d->block_size=block_size;
    d->block_extra=0;
    d->blocks_offset=off_blocks;
    d->blocks_count=blocks;
    d->blocks_allocated=allocated;
    d->image_type=type;
    return 0;
}

static int init_header(unsigned char h[VDI_HEADER_BYTES], uint64_t size_bytes,
                       uint32_t image_type, uint32_t blocks,
                       uint32_t off_blocks, uint32_t off_data,
                       const unsigned char image_uuid[16]) {
    unsigned char uuid[16];
    memset(h,0,VDI_HEADER_BYTES);
    memcpy(h,"<<< Guppy VirtualBox Disk Image >>>\n",35);
    put32(h+0x40,VDI_SIGNATURE);
    put32(h+0x44,VDI_VERSION_1_1);
    put32(h+0x48,0x180U);
    put32(h+0x4c,image_type);
    memcpy(h+0x54,image_type==VDI_IMAGE_TYPE_FIXED ?
           "Guppy fixed VDI" : "Guppy sparse VDI",15);
    put32(h+0x154,off_blocks);
    put32(h+0x158,off_data);
    put32(h+0x168,VDI_SECTOR_SIZE);
    put64(h+0x170,size_bytes);
    put32(h+0x178,VDI_BLOCK_SIZE);
    put32(h+0x17c,0);
    put32(h+0x180,blocks);
    put32(h+0x184,image_type==VDI_IMAGE_TYPE_FIXED ? blocks : 0);
    if(image_uuid) memcpy(h+0x188,image_uuid,16);
    else { make_uuid(uuid); memcpy(h+0x188,uuid,16); }
    make_uuid(uuid); memcpy(h+0x198,uuid,16);
    return 0;
}

static int layout_for(uint64_t *size_bytes_io, uint32_t *blocks_out,
                      uint32_t *off_blocks_out, uint32_t *off_data_out) {
    uint64_t size_bytes=*size_bytes_io;
    uint64_t map_bytes,map_padded;
    uint32_t blocks;

    if(!size_bytes || size_bytes>UINT64_MAX-(VDI_SECTOR_SIZE-1)) return -1;
    size_bytes=round_up_u64(size_bytes,VDI_SECTOR_SIZE);
    if((size_bytes+VDI_BLOCK_SIZE-1)/VDI_BLOCK_SIZE > UINT32_MAX) return -1;
    blocks=(uint32_t)((size_bytes+VDI_BLOCK_SIZE-1)/VDI_BLOCK_SIZE);
    if(!blocks) return -1;

    map_bytes=(uint64_t)blocks*4U;
    map_padded=round_up_u64(map_bytes,VDI_SECTOR_SIZE);
    if(map_padded>UINT32_MAX-VDI_HEADER_BYTES) return -1;

    *size_bytes_io=size_bytes;
    *blocks_out=blocks;
    *off_blocks_out=VDI_HEADER_BYTES;
    *off_data_out=(uint32_t)(VDI_HEADER_BYTES+map_padded);
    return 0;
}

static int write_map(vdisk_t *d, uint32_t blocks, uint32_t off_blocks,
                     uint32_t off_data, int fixed) {
    unsigned char e[4],zero[4096];
    uint64_t map_bytes=(uint64_t)blocks*4U;
    uint64_t pos;
    uint32_t i;

    for(i=0;i<blocks;i++) {
        put32(e,fixed ? i : VDI_BLOCK_UNALLOCATED);
        if(vdisk_host_write(d->fp,(uint64_t)off_blocks+(uint64_t)i*4U,e,4)!=0)
            return -1;
    }
    memset(zero,0,sizeof zero);
    pos=(uint64_t)off_blocks+map_bytes;
    while(pos<off_data) {
        size_t n=(size_t)(((uint64_t)off_data-pos)>sizeof(zero)?
                          sizeof(zero):((uint64_t)off_data-pos));
        if(vdisk_host_write(d->fp,pos,zero,n)!=0) return -1;
        pos+=n;
    }
    return 0;
}

static int vdi_create_fixed(vdisk_t *d, uint64_t size_bytes) {
    unsigned char h[VDI_HEADER_BYTES];
    uint32_t blocks,off_blocks,off_data;
    uint64_t host_size;

    if(!d || !d->fp) return -1;
    if(layout_for(&size_bytes,&blocks,&off_blocks,&off_data)!=0) return -1;
    if(size_bytes>UINT64_MAX-off_data) return -1;
    host_size=(uint64_t)off_data+size_bytes;

    init_header(h,size_bytes,VDI_IMAGE_TYPE_FIXED,blocks,off_blocks,off_data,d->create_uuid_set?d->create_uuid:NULL);
    if(vdisk_host_write(d->fp,0,h,sizeof h)!=0) return -1;
    if(write_map(d,blocks,off_blocks,off_data,1)!=0) return -1;
    if(vdisk_host_seek(d->fp,host_size-1)!=0 ||
       fputc(0,d->fp)==EOF || fflush(d->fp)!=0) return -1;

    d->size_bytes=size_bytes;
    d->host_size_bytes=host_size;
    d->data_offset=off_data;
    d->block_size=VDI_BLOCK_SIZE;
    d->block_extra=0;
    d->blocks_offset=off_blocks;
    d->blocks_count=blocks;
    d->blocks_allocated=blocks;
    d->image_type=VDI_IMAGE_TYPE_FIXED;
    return 0;
}

static int vdi_create_sparse(vdisk_t *d, uint64_t size_bytes) {
    unsigned char h[VDI_HEADER_BYTES];
    uint32_t blocks,off_blocks,off_data;

    if(!d || !d->fp) return -1;
    if(layout_for(&size_bytes,&blocks,&off_blocks,&off_data)!=0) return -1;

    init_header(h,size_bytes,VDI_IMAGE_TYPE_DYNAMIC,blocks,off_blocks,off_data,d->create_uuid_set?d->create_uuid:NULL);
    if(vdisk_host_write(d->fp,0,h,sizeof h)!=0) return -1;
    if(write_map(d,blocks,off_blocks,off_data,0)!=0) return -1;

    /*
     * write_map() already materializes the complete metadata region through
     * off_data.  Do not touch off_data-1 here: when the block map exactly
     * fills its sector-aligned area (for example a 1 GiB VDI with 1024 map
     * entries), off_data-1 is the final byte of the final map entry.
     */
    if(fflush(d->fp)!=0) return -1;

    d->size_bytes=size_bytes;
    d->host_size_bytes=off_data;
    d->data_offset=off_data;
    d->block_size=VDI_BLOCK_SIZE;
    d->block_extra=0;
    d->blocks_offset=off_blocks;
    d->blocks_count=blocks;
    d->blocks_allocated=0;
    d->image_type=VDI_IMAGE_TYPE_DYNAMIC;
    return 0;
}

static int bounds_ok(const vdisk_t*d,uint64_t off,size_t len) {
    return off<=d->size_bytes && (uint64_t)len<=d->size_bytes-off;
}

static int map_get(vdisk_t *d, uint32_t guest_block, uint32_t *entry) {
    unsigned char e[4];
    if(!entry || guest_block>=d->blocks_count) return -1;
    if(vdisk_host_read(d->fp,d->blocks_offset+(uint64_t)guest_block*4U,e,4)!=0)
        return -1;
    *entry=le32(e);
    return 0;
}

static int map_set(vdisk_t *d, uint32_t guest_block, uint32_t entry) {
    unsigned char e[4];
    if(guest_block>=d->blocks_count) return -1;
    put32(e,entry);
    return vdisk_host_write(d->fp,d->blocks_offset+(uint64_t)guest_block*4U,e,4);
}

static int update_allocated(vdisk_t *d) {
    unsigned char e[4];
    put32(e,d->blocks_allocated);
    return vdisk_host_write(d->fp,0x184,e,4);
}

static int allocate_block(vdisk_t *d, uint32_t guest_block, uint32_t *entry_out) {
    unsigned char zero[65536];
    uint32_t entry=d->blocks_allocated;
    uint64_t host_off;
    uint64_t done=0;

    if(entry>=d->blocks_count) return -1;
    if((uint64_t)entry > (UINT64_MAX-d->data_offset)/d->block_size) return -1;
    host_off=d->data_offset+(uint64_t)entry*d->block_size;
    memset(zero,0,sizeof zero);

    /* Materialize a clean VDI block before exposing it in the block map. */
    while(done<d->block_size) {
        size_t n=(size_t)(((uint64_t)d->block_size-done)>sizeof zero ?
                          sizeof zero : ((uint64_t)d->block_size-done));
        if(vdisk_host_write(d->fp,host_off+done,zero,n)!=0) return -1;
        done+=n;
    }
    if(map_set(d,guest_block,entry)!=0) return -1;
    d->blocks_allocated++;
    if(update_allocated(d)!=0) return -1;
    d->host_size_bytes=d->data_offset+(uint64_t)d->blocks_allocated*d->block_size;
    if(entry_out) *entry_out=entry;
    return 0;
}

static int vdi_read(vdisk_t*d,uint64_t off,void*buf,size_t len) {
    unsigned char *out=(unsigned char*)buf;

    if(!bounds_ok(d,off,len)) return -1;
    if(d->image_type==VDI_IMAGE_TYPE_FIXED)
        return vdisk_host_read(d->fp,d->data_offset+off,buf,len);

    while(len) {
        uint32_t guest_block=(uint32_t)(off/d->block_size);
        uint32_t in_block=(uint32_t)(off%d->block_size);
        size_t n=d->block_size-in_block;
        uint32_t entry;
        if(n>len) n=len;
        if(map_get(d,guest_block,&entry)!=0) return -1;
        if(entry==VDI_BLOCK_UNALLOCATED) {
            memset(out,0,n);
        } else {
            if(entry>=d->blocks_allocated) return -1;
            if(vdisk_host_read(d->fp,d->data_offset+
                               (uint64_t)entry*d->block_size+in_block,out,n)!=0)
                return -1;
        }
        off+=n; out+=n; len-=n;
    }
    return 0;
}

static int vdi_write(vdisk_t*d,uint64_t off,const void*buf,size_t len) {
    const unsigned char *in=(const unsigned char*)buf;

    if(!d->writable || !bounds_ok(d,off,len)) return -1;
    if(d->image_type==VDI_IMAGE_TYPE_FIXED)
        return vdisk_host_write(d->fp,d->data_offset+off,buf,len);

    while(len) {
        uint32_t guest_block=(uint32_t)(off/d->block_size);
        uint32_t in_block=(uint32_t)(off%d->block_size);
        size_t n=d->block_size-in_block;
        uint32_t entry;
        if(n>len) n=len;
        if(map_get(d,guest_block,&entry)!=0) return -1;
        if(entry==VDI_BLOCK_UNALLOCATED) {
            if(allocate_block(d,guest_block,&entry)!=0) return -1;
        } else if(entry>=d->blocks_allocated) {
            return -1;
        }
        if(vdisk_host_write(d->fp,d->data_offset+
                           (uint64_t)entry*d->block_size+in_block,in,n)!=0)
            return -1;
        off+=n; in+=n; len-=n;
    }
    return 0;
}

static int vdi_flush(vdisk_t*d){return fflush(d->fp)==0?0:-1;}
static void vdi_close(vdisk_t*d){(void)d;}

const vdisk_driver_t vdisk_vdi_driver={
    VDISK_FMT_VDI,"vdi",vdi_probe,vdi_open,vdi_create_fixed,vdi_create_sparse,
    vdi_read,vdi_write,vdi_flush,vdi_close
};


int vdisk_vdi_inspect(FILE *fp, vdisk_info_t *info) {
    unsigned char h[VDI_HEADER_BYTES];
    uint32_t sig,version,hdrsz,type,off_blocks,off_data,sector_size;
    uint32_t block_size,block_extra,blocks,allocated;
    uint64_t disk_size,host_size,map_end,required;
    uint32_t i;
    unsigned char e[4];
    unsigned char *seen=NULL;

#define CHECK(c) do { if (c) info->checks_passed++; else info->checks_failed++; } while (0)

    if(!fp || !info) return -1;
    memset(info,0,sizeof(*info));
    info->format=VDISK_FMT_VDI;
    info->supported=true;

    if(read_header(fp,h)!=0 || vdisk_host_get_size(fp,&host_size)!=0) return -1;
    sig=le32(h+0x40); version=le32(h+0x44); hdrsz=le32(h+0x48);
    type=le32(h+0x4c); off_blocks=le32(h+0x154); off_data=le32(h+0x158);
    sector_size=le32(h+0x168); disk_size=le64(h+0x170);
    block_size=le32(h+0x178); block_extra=le32(h+0x17c);
    blocks=le32(h+0x180); allocated=le32(h+0x184);

    info->host_size_bytes=host_size;
    info->logical_size_bytes=disk_size;
    info->data_offset=off_data;
    info->blocks_offset=off_blocks;
    info->image_type=type;
    info->sector_size=sector_size;
    info->block_size=block_size;
    info->blocks_count=blocks;
    info->blocks_allocated=allocated;
    if(type==VDI_IMAGE_TYPE_DYNAMIC) strcpy(info->variant,"dynamic/sparse");
    else if(type==VDI_IMAGE_TYPE_FIXED) strcpy(info->variant,"fixed/full");
    else strcpy(info->variant,"unknown");

    CHECK(sig==VDI_SIGNATURE);
    CHECK(version==VDI_VERSION_1_1);
    CHECK(hdrsz>=0x180U && hdrsz<=VDI_HEADER_BYTES);
    CHECK(type==VDI_IMAGE_TYPE_FIXED || type==VDI_IMAGE_TYPE_DYNAMIC);
    CHECK(sector_size==VDI_SECTOR_SIZE);
    CHECK(disk_size!=0);
    CHECK(block_size!=0);
    CHECK(block_extra==0);
    CHECK(blocks!=0);
    CHECK(allocated<=blocks);
    CHECK(off_blocks>=VDI_HEADER_BYTES);
    CHECK(off_data>off_blocks);

    map_end=(uint64_t)off_blocks+(uint64_t)blocks*4U;
    CHECK(map_end<=off_data);
    CHECK(off_data<=host_size);
    if(block_size && blocks) CHECK(disk_size<=(uint64_t)blocks*block_size);
    else CHECK(0);

    if(type==VDI_IMAGE_TYPE_FIXED && block_size) {
        CHECK(allocated==blocks);
        required=(uint64_t)off_data+disk_size;
        CHECK(required>=off_data && host_size>=required);
    } else if(type==VDI_IMAGE_TYPE_DYNAMIC && block_size) {
        required=(uint64_t)off_data+(uint64_t)allocated*block_size;
        CHECK(required>=off_data && host_size>=required);
    }

    if(blocks && map_end<=host_size) {
        if(blocks <= 16U*1024U*1024U)
            seen=(unsigned char*)calloc((size_t)blocks,1);
        for(i=0;i<blocks;i++) {
            uint32_t v;
            if(vdisk_host_read(fp,(uint64_t)off_blocks+(uint64_t)i*4U,e,4)!=0) {
                info->map_invalid_entries++;
                continue;
            }
            v=le32(e);
            if(v==VDI_BLOCK_UNALLOCATED) {
                info->map_unallocated_entries++;
                if(type==VDI_IMAGE_TYPE_FIXED) info->map_invalid_entries++;
                continue;
            }
            info->map_allocated_entries++;
            if(v>=allocated || v>=blocks) {
                info->map_invalid_entries++;
                continue;
            }
            if(seen) {
                if(seen[v]) info->map_duplicate_entries++;
                seen[v]=1;
            }
        }
        CHECK(info->map_invalid_entries==0);
        CHECK(info->map_duplicate_entries==0);
        CHECK(info->map_allocated_entries==allocated);
        if(type==VDI_IMAGE_TYPE_DYNAMIC)
            CHECK(info->map_unallocated_entries==blocks-allocated);
        else
            CHECK(info->map_unallocated_entries==0);
    } else {
        info->checks_failed += 4;
    }
    free(seen);
    info->structurally_valid=(info->checks_failed==0);
#undef CHECK
    return 0;
}
