#pragma once
#include <stdint.h>
#include <stdbool.h>

// -------- FAT formatter (real, simple) ----------
typedef struct {
    const char *image_path;   // required
    uint32_t    lba_offset;   // default 0
    uint16_t    bytes_per_sec; // default 512
    uint8_t     sec_per_clus; // 0 = auto
    int         fat_type;     // 12,16,32; -1 = auto
    const char *label;        // default "NO NAME"
    const char *oem;          // default "MSWIN4.1"
    int         verbose;      // 0/1
} mkfs_fat_opts_t;

// returns 0 on success, nonzero on error
int mkfs_fat_format(const mkfs_fat_opts_t *opt);

// -------- NTFS minimal (probe-only) ------------
typedef struct {
    const char *image_path;     // required
    uint32_t    lba_offset;     // default 0
    uint16_t    bytes_per_sec;  // default 512
    uint8_t     sec_per_clus;   // default 8 (4 KiB clusters on 512B sectors)
    uint32_t    mft_start_clus; // default 4
    uint32_t    mftmirr_clus;   // default 8
    int         verbose;        // 0/1
} mkfs_ntfs_opts_t;

int mkfs_ntfs_core(const mkfs_ntfs_opts_t *opt);
