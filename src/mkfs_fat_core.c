// src/mkfs_fat_core.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "fs_format.h"

// ---------- helpers ----------
static inline void pad_copy(char *dst, size_t n, const char *src) {
    memset(dst, ' ', n);
    if (!src) return;
    size_t m = strlen(src);
    if (m > n) m = n;
    memcpy(dst, src, m);
}

static uint64_t fsize(FILE *f){
    long cur=ftell(f); fseek(f,0,SEEK_END); long end=ftell(f); fseek(f,cur,SEEK_SET);
    return (uint64_t)end;
}

static void zero_region(FILE *f, uint64_t off, uint64_t len){
    static uint8_t z[4096]={0};
    fseek(f,(long)off,SEEK_SET);
    while (len){
        size_t n = (len > sizeof z)? sizeof z : (size_t)len;
        fwrite(z,1,n,f);
        len -= n;
    }
}

static void put_sector(FILE *f, uint32_t bps, uint32_t lba, const void *buf){
    fseek(f,(long)((uint64_t)lba*bps),SEEK_SET);
    fwrite(buf,1,bps,f);
}

static uint32_t ceil_div(uint32_t a, uint32_t b){ return (a + b - 1)/b; }

// ---------- on-disk structs ----------
#pragma pack(push,1)
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bps;              // bytes per sector
    uint8_t  spc;              // sectors per cluster
    uint16_t rsvd;             // reserved sectors
    uint8_t  nfats;            // number of FATs (2)
    uint16_t root_entries;     // 512 for FAT12/16, 0 for FAT32
    uint16_t totsec16;         // if < 65536 else 0 and use totsec32
    uint8_t  media;            // 0xF8 fixed
    uint16_t fatsz16;          // FAT size (sectors) for FAT12/16
    uint16_t spt;              // sectors per track (BIOS hint)
    uint16_t heads;            // heads (BIOS hint)
    uint32_t hidden;           // LBA of partition start
    uint32_t totsec32;         // if totsec16 == 0
    union {
        struct {
            uint8_t  drv, ntflg, sig; // sig=0x28/0x29
            uint32_t volid;
            char     label[11];       // no NUL, space-padded
            char     fstype[8];       // "FAT12   "/"FAT16   "
        } f16;
        struct {
            uint32_t fatsz32;         // sectors per FAT
            uint16_t ext_flags, fsver;
            uint32_t rootclus;        // usually 2
            uint16_t fsinfo;          // usually 1
            uint16_t bkboot;          // usually 6
            uint8_t  rsvd2[12];
            uint8_t  drv, ntflg, sig; // sig=0x29
            uint32_t volid;
            char     label[11];       // no NUL, space-padded
            char     fstype[8];       // "FAT32   "
        } f32;
    } u;
    uint8_t  bootcode[420];
    uint16_t sig55aa;          // 0xAA55
} bpb_t;

typedef struct {
    uint32_t lead;     // 0x41615252
    uint8_t  pad1[480];
    uint32_t sig;      // 0x61417272
    uint32_t freec;    // 0xFFFFFFFF (unknown)
    uint32_t nextf;    // 0xFFFFFFFF (unknown)
    uint8_t  pad2[12];
    uint32_t trail;    // 0xAA550000
} fsinfo_t;
#pragma pack(pop)

// ---------- mkfs implementation ----------
int mkfs_fat_format(const mkfs_fat_opts_t *opt){
    if (!opt || !opt->image_path) {
        fprintf(stderr,"mkfs_fat: bad args\n");
        return 2;
    }
    const char *label = opt->label ? opt->label : "NO NAME   ";
    const char *oem   = opt->oem   ? opt->oem   : "MSWIN4.1";
    uint16_t bps = opt->bytes_per_sec ? opt->bytes_per_sec : 512;
    uint8_t  spc = opt->sec_per_clus; // may be 0 (auto)
    int F = (opt->fat_type==12||opt->fat_type==16||opt->fat_type==32) ? opt->fat_type : -1;

    FILE *f = fopen(opt->image_path,"r+b");
    if(!f){ perror("open"); return 1; }

    uint64_t bytes = fsize(f);
    if (bytes < 100ull * 512){
        fprintf(stderr,"mkfs_fat: image too small\n");
        fclose(f);
        return 3;
    }

    if (F<0){
        if (bytes < 16ull*1024*1024) F=12;
        else if (bytes < 512ull*1024*1024) F=16;
        else F=32;
    }
    if (!spc) spc = (F==32)? 8 : 4;

    bpb_t b; memset(&b,0,sizeof b);
    b.jmp[0]=0xEB; b.jmp[1]=0x3C; b.jmp[2]=0x90;
    pad_copy(b.oem, 8, oem);
    b.bps = bps;
    b.spc = spc;
    b.rsvd = (F==32)? 32 : 1;
    b.nfats = 2;
    b.media = 0xF8;
    b.spt = 32;
    b.heads = 64;
    b.hidden = opt->lba_offset;

    uint32_t totsec = (uint32_t)(bytes / bps);
    if (totsec < 65536) { b.totsec16 = (uint16_t)totsec; b.totsec32 = 0; }
    else { b.totsec16 = 0; b.totsec32 = totsec; }

    if (F==32){
        // ----- FAT32 layout -----
        b.root_entries = 0;

        // estimate FAT size
        uint32_t data_guess = totsec - b.rsvd - 1; // minus at least a sector for FAT
        uint32_t clus_guess = data_guess / b.spc;
        uint32_t fatsz32 = ceil_div(clus_guess * 4, bps);
        if (fatsz32 < 1) fatsz32 = 1;

        b.u.f32.fatsz32 = fatsz32;
        b.u.f32.rootclus = 2;
        b.u.f32.fsinfo = 1;
        b.u.f32.bkboot = 6;
        b.u.f32.drv = 0x80;
        b.u.f32.sig = 0x29;
        b.u.f32.volid = 0x12345678;
        pad_copy(b.u.f32.label, 11, label);
        memcpy(b.u.f32.fstype, "FAT32   ", 8);
        b.sig55aa = 0xAA55;

        // write boot sector, FSINFO, and backup boot
        put_sector(f,bps,opt->lba_offset,&b);

        fsinfo_t fi; memset(&fi,0,sizeof fi);
        fi.lead=0x41615252; fi.sig=0x61417272; fi.freec=0xFFFFFFFF; fi.nextf=0xFFFFFFFF; fi.trail=0xAA550000;
        put_sector(f,bps,opt->lba_offset + 1,&fi);
        put_sector(f,bps,opt->lba_offset + b.u.f32.bkboot,&b);

        // zero FATs
        uint32_t fat1 = opt->lba_offset + b.rsvd;
        uint32_t fatsz = b.u.f32.fatsz32;
        uint32_t fat2 = fat1 + fatsz;
        zero_region(f,(uint64_t)fat1*bps,(uint64_t)fatsz*bps);
        zero_region(f,(uint64_t)fat2*bps,(uint64_t)fatsz*bps);

        // FAT[0..2] reserved entries: media + EOCs + root cluster EOC
        uint8_t head[12]={0};
        head[0]=0xF8; head[1]=0xFF; head[2]=0xFF; head[3]=0x0F;
        head[4]=0xFF; head[5]=0xFF; head[6]=0xFF; head[7]=0x0F;
        head[8]=0xFF; head[9]=0xFF; head[10]=0xFF; head[11]=0x0F;
        fseek(f,(long)((uint64_t)fat1*bps),SEEK_SET); fwrite(head,1,sizeof head,f);
        fseek(f,(long)((uint64_t)fat2*bps),SEEK_SET); fwrite(head,1,sizeof head,f);

    } else {
        // ----- FAT12/16 layout -----
        b.root_entries = 512;
        uint32_t root_secs = ceil_div(b.root_entries*32, bps);
        uint32_t data_secs = totsec - b.rsvd - root_secs;
        uint32_t clus_guess = data_secs / b.spc;

        uint32_t fat_bytes = (F==12) ? ((clus_guess*3+1)/2) : (clus_guess*2);
        uint16_t fatsz16 = (uint16_t)ceil_div(fat_bytes, bps);
        if (fatsz16 < 1) fatsz16 = 1;

        b.fatsz16 = fatsz16;
        b.u.f16.drv=0x80; b.u.f16.sig=0x29; b.u.f16.volid=0x12345678;
        pad_copy(b.u.f16.label, 11, label);
        memcpy(b.u.f16.fstype,(F==12)?"FAT12   ":"FAT16   ",8);
        b.sig55aa = 0xAA55;

        // write boot
        put_sector(f,bps,opt->lba_offset,&b);

        // compute LBAs
        uint32_t fat1 = opt->lba_offset + b.rsvd;
        uint32_t fatsz = b.fatsz16;
        uint32_t fat2 = fat1 + fatsz;
        uint32_t root = fat2 + fatsz;

        // zero FATs + root dir region
        zero_region(f,(uint64_t)fat1*bps,(uint64_t)fatsz*bps);
        zero_region(f,(uint64_t)fat2*bps,(uint64_t)fatsz*bps);
        zero_region(f,(uint64_t)root*bps,(uint64_t)root_secs*bps);

        // write FAT head (reserved entries)
        if (F==12){
            uint8_t h[3]={0xF8,0xFF,0xFF};
            fseek(f,(long)((uint64_t)fat1*bps),SEEK_SET); fwrite(h,1,3,f);
            fseek(f,(long)((uint64_t)fat2*bps),SEEK_SET); fwrite(h,1,3,f);
        } else {
            uint8_t h[4]={0xF8,0xFF,0xFF,0xFF};
            fseek(f,(long)((uint64_t)fat1*bps),SEEK_SET); fwrite(h,1,4,f);
            fseek(f,(long)((uint64_t)fat2*bps),SEEK_SET); fwrite(h,1,4,f);
        }
    }

    if (opt->verbose)
        printf("mkfs.fat: %s (FAT%d) bps=%u spc=%u lba_off=%u\n",
               opt->image_path,
               (F<0?32:F), bps, spc, opt->lba_offset);

    fclose(f);
    return 0;
}
