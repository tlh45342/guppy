#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "fs_format.h"

#define NTFS_OEM "NTFS    "
#define BOOT_JMP0 0xEB
#define BOOT_JMP1 0x52
#define BOOT_JMP2 0x90

#pragma pack(push,1)
// NTFS BPB (boot sector) 512 bytes
typedef struct {
    uint8_t  jmp[3];         // EB 52 90
    char     oem[8];         // "NTFS    "
    uint16_t bps;            // bytes/sector
    uint8_t  spc;            // sectors/cluster (power of two)
    uint16_t rsvd;           // always 0 on NTFS
    uint8_t  zero1[5];       // 0
    uint8_t  media;          // 0xF8
    uint8_t  zero2[2];       // 0
    uint16_t spt;            // sectors per track (BIOS)
    uint16_t heads;          // number of heads (BIOS)
    uint32_t hidden;         // LBA offset (if partitioned)
    uint32_t unused;         // 0
    uint64_t totsec;         // total sectors
    uint64_t mft_clus;       // cluster index of $MFT
    uint64_t mftmirr_clus;   // cluster index of $MFTMirr
    int8_t   clus_per_mft;   // -10 => 2^10 bytes/FILE record (1024)
    uint8_t  pad1[3];
    int8_t   clus_per_index; // usually 1 (or -10 => 1KiB)
    uint8_t  pad2[3];
    uint64_t serial;         // volume serial
    uint32_t bscsum;         // boot sector checksum (we leave 0)
    uint8_t  bootstrap[426]; // minimal zeroed stub
    uint16_t sig55aa;        // 0xAA55
} ntfs_bpb_t;

// Minimal NTFS "FILE" record header (1024 bytes record, we fill some fields)
typedef struct {
    char     magic[4];       // "FILE"
    uint16_t usa_ofs;        // Update Sequence Array offset
    uint16_t usa_count;      // USA count (1 + number of 512B sectors in record)
    uint64_t lsn;            // $LogFile sequence number (0 ok)
    uint16_t seq_no;         // sequence number
    uint16_t hardlinks;      // hard link count
    uint16_t attr_ofs;       // first attribute offset
    uint16_t flags;          // 0x0001 in-use, 0x0002 directory
    uint32_t bytes_real;     // real size of record (<= 1024)
    uint32_t bytes_alloc;    // allocated size of record (1024)
    uint64_t base_file;      // Base MFT ref (for extents) (0 for base)
    uint16_t next_attr_id;   // next attribute id
    uint16_t align;          // alignment
    uint32_t mft_record_no;  // record index
    // attributes follow...
} ntfs_file_rec_t;
#pragma pack(pop)

static uint64_t fsize(FILE *f){
    long cur=ftell(f); fseek(f,0,SEEK_END); long end=ftell(f); fseek(f,cur,SEEK_SET);
    return (uint64_t)end;
}

static void zero_region(FILE *f, uint64_t off, uint64_t len){
    static uint8_t z[4096]={0};
    fseek(f,(long)off,SEEK_SET);
    while (len){
        size_t n=(len>sizeof z)? sizeof z : (size_t)len;
        fwrite(z,1,n,f);
        len -= n;
    }
}

static void put_sector(FILE *f, uint32_t bps, uint64_t lba, const void *buf, size_t len){
    fseek(f,(long)((lba)*bps),SEEK_SET);
    fwrite(buf,1,len,f);
}

typedef struct {
    uint32_t bps, spc;
    uint64_t total_sectors;
    uint64_t lba_off;
    uint64_t mft_lcn;       // logical cluster number of $MFT
    uint64_t mftmirr_lcn;   // logical cluster number of $MFTMirr
    uint32_t bytes_per_cluster;
    uint32_t bytes_per_mftrec; // 1024 (with clus_per_mft = -10)
} layout_t;

static void plan_layout(const mkfs_ntfs_opts_t *opt, uint64_t totsec, layout_t *L){
    memset(L,0,sizeof *L);
    L->bps = opt->bytes_per_sec ? opt->bytes_per_sec : 512;
    L->spc = opt->sec_per_clus ? opt->sec_per_clus : 8;  // 4KiB clusters on 512B
    L->bytes_per_cluster = L->bps * L->spc;
    L->bytes_per_mftrec = 1024; // fixed for now
    L->total_sectors = totsec;
    L->lba_off = opt->lba_offset;
    L->mft_lcn = opt->mft_start_clus ? opt->mft_start_clus : 4;
    L->mftmirr_lcn = opt->mftmirr_clus ? opt->mftmirr_clus : 8;
}

static void write_boot_sector(FILE *f, const layout_t *L, const mkfs_ntfs_opts_t *opt){
	(void)opt;  // silence -Wunused-parameter
    ntfs_bpb_t b; memset(&b,0,sizeof b);
    b.jmp[0]=BOOT_JMP0; b.jmp[1]=BOOT_JMP1; b.jmp[2]=BOOT_JMP2;
    memcpy(b.oem, NTFS_OEM, 8);
    b.bps = (uint16_t)L->bps;
    b.spc = (uint8_t)L->spc;
    b.media = 0xF8;
    b.spt = 32;
    b.heads = 64;
    b.hidden = (uint32_t)L->lba_off;
    b.totsec = L->total_sectors;
    b.mft_clus = L->mft_lcn;
    b.mftmirr_clus = L->mftmirr_lcn;
    b.clus_per_mft = -10; // 2^10 = 1024 bytes records
    b.clus_per_index = 1; // simple
    b.serial = 0x1122334455667788ULL;
    b.sig55aa = 0xAA55;

    // Primary boot sector @ lba_off
    put_sector(f, L->bps, L->lba_off, &b, sizeof b);

    // NTFS keeps backup boot at last sector of the volume
    put_sector(f, L->bps, L->lba_off + (L->total_sectors - 1), &b, sizeof b);
}

// Simple helper: seed a minimal “FILE” record header with USA fixups.
// NOTE: Real NTFS requires proper USA fixup mapping every 512 bytes.
// We implement a correct USA header for a 1024-byte record (2 sectors).
static void write_mft_record_stub(FILE *f, const layout_t *L, uint64_t byte_off, uint32_t rec_no, bool mark_directory){
    const uint32_t rec_bytes = L->bytes_per_mftrec; // 1024
    uint8_t buf[1024]; memset(buf,0,sizeof buf);

    ntfs_file_rec_t *fr = (ntfs_file_rec_t*)buf;
    memcpy(fr->magic, "FILE", 4);

    // USA header goes right after header: we'll put it at offset 0x30
    const uint16_t usa_offset = 0x30;
    const uint16_t usa_count  = 1 + (rec_bytes / 512); // 1 + number of sectors
    fr->usa_ofs   = usa_offset;
    fr->usa_count = usa_count;
    fr->seq_no    = 1;
    fr->hardlinks = 1;
    fr->attr_ofs  = 0x38; // (no real attributes yet; placeholder)
    fr->flags     = (mark_directory? 0x0003 : 0x0001); // in-use [+ directory flag]
    fr->bytes_real= rec_bytes;
    fr->bytes_alloc = rec_bytes;
    fr->base_file = 0;
    fr->next_attr_id = 0x10;
    fr->mft_record_no = rec_no;

    // Prepare USA fixups: 2 sectors -> 2 fixups + signature word
    // The Update Sequence Number is any 16-bit value (increment okay). Use 0xAAAA.
    uint16_t *usa = (uint16_t*)(buf + usa_offset);
    usa[0] = 0xAAAA;
    // save original last 2 bytes of each 512B sector and overwrite them with USN
    // then store the originals in the USA array.
    uint16_t *sec1_tail = (uint16_t*)(buf + 512 - 2);
    uint16_t *sec2_tail = (uint16_t*)(buf + 1024 - 2);
    uint16_t orig1 = *sec1_tail;
    uint16_t orig2 = *sec2_tail;
    usa[1] = orig1;
    usa[2] = orig2;
    *sec1_tail = usa[0];
    *sec2_tail = usa[0];

    // END marker for attributes (0xFFFFFFFF) at fr->bytes_real-8 is typical,
    // but since we have no attrs we can place it at attr_ofs.
    *(uint32_t*)(buf + fr->attr_ofs) = 0xFFFFFFFF;

    fseek(f, (long)byte_off, SEEK_SET);
    fwrite(buf,1,sizeof buf,f);
}

int mkfs_ntfs_core(const mkfs_ntfs_opts_t *opt){
    if (!opt || !opt->image_path){ fprintf(stderr,"mkfs.ntfs(core): bad args\n"); return 2; }

    uint16_t bps = opt->bytes_per_sec ? opt->bytes_per_sec : 512;
    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096){
        fprintf(stderr,"mkfs.ntfs(core): bytes/sector must be 512/1024/2048/4096\n");
        return 2;
    }

    FILE *f = fopen(opt->image_path,"r+b");
    if (!f){ perror("open"); return 1; }

    uint64_t bytes_total = fsize(f);
    if (bytes_total < (uint64_t)bps * 100){
        fclose(f);
        fprintf(stderr,"mkfs.ntfs(core): image too small\n");
        return 3;
    }

    uint64_t totsec = bytes_total / bps;

    layout_t L;
    plan_layout(opt, totsec, &L);

    // 1) Write primary & backup boot sectors
    write_boot_sector(f, &L, opt);

    // 2) Reserve/zero a small region for $MFT and $MFTMirr
    //    We'll carve 16 records (16 * 1KiB = 16KiB) for $MFT seed,
    //    and 4 records for $MFTMirr.
    uint32_t mft_seed_records = 16;
    uint32_t mftmirr_records  = 4;
    uint64_t mft_byte_off     = (L.lba_off * (uint64_t)L.bps) + (L.mft_lcn * (uint64_t)L.bytes_per_cluster);
    uint64_t mftmirr_byte_off = (L.lba_off * (uint64_t)L.bps) + (L.mftmirr_lcn * (uint64_t)L.bytes_per_cluster);
    zero_region(f, mft_byte_off,     (uint64_t)mft_seed_records    * L.bytes_per_mftrec);
    zero_region(f, mftmirr_byte_off, (uint64_t)mftmirr_records     * L.bytes_per_mftrec);

    // 3) Seed $MFT[0] and $MFTMirr[1] minimal records
    //    NOTE: This is a **stub** FILE record with valid "FILE" header + USA fixups.
    //    Real NTFS requires $STANDARD_INFORMATION + $FILE_NAME attributes, etc.
    write_mft_record_stub(f, &L, mft_byte_off + 0*L.bytes_per_mftrec, 0, false); // $MFT
    write_mft_record_stub(f, &L, mft_byte_off + 1*L.bytes_per_mftrec, 1, false); // $MFTMirr
    // Mirror copies (usually first 4 records mirrored). We'll mirror record 0 and 1:
    write_mft_record_stub(f, &L, mftmirr_byte_off + 0*L.bytes_per_mftrec, 0, false);
    write_mft_record_stub(f, &L, mftmirr_byte_off + 1*L.bytes_per_mftrec, 1, false);

    // (Optional) Pre-zero a tiny slice for $Bitmap and $LogFile to make later work easier
    // Choose some fixed LCNs after MFT region:
    uint64_t after_mft_bytes = mft_byte_off + (uint64_t)mft_seed_records * L.bytes_per_mftrec;
    zero_region(f, after_mft_bytes, 256 * 1024); // 256 KiB scratch for future metadata

    fclose(f);

    if (opt->verbose){
        printf("mkfs.ntfs(core): %s\n", opt->image_path);
        printf("  bps=%u spc=%u cluster=%u B  total_sectors=%llu\n",
               L.bps, L.spc, L.bytes_per_cluster, (unsigned long long)L.total_sectors);
        printf("  mft_lcn=%llu (byte_off=%llu), mftmirr_lcn=%llu\n",
               (unsigned long long)L.mft_lcn, (unsigned long long)mft_byte_off,
               (unsigned long long)L.mftmirr_lcn);
        printf("  seeded MFT records: 0 ($MFT), 1 ($MFTMirr) + mirrored\n");
    }

    return 0;
}
