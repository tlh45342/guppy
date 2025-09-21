#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "fs_format.h"
#include "devmap.h"

static int is_pow2_u16(unsigned v){ return v && ((v & (v-1)) == 0); }
static int is_pow2_u8 (unsigned v){ return v && v <= 128 && ((v & (v-1)) == 0); }

// usage: mkfs.ntfs /dev/a [-o lba] [-S bps] [-c spc] [--mft lcn] [--mftmirr lcn] [-v]
int cmd_mkfs_ntfs(int argc, char **argv){
    if (argc < 2){
        printf("usage: mkfs.ntfs /dev/X [-o lba] [-S bps] [-c spc] [--mft lcn] [--mftmirr lcn] [-v]\n");
        return 2;
    }
    const char *dev = argv[1];
    if (strncmp(dev, "/dev/", 5) != 0){
        printf("mkfs.ntfs: first argument must be a mapped device like /dev/a\n");
        return 2;
    }

    const char *mapped = devmap_resolve(dev);
    if (!mapped){
        printf("mkfs.ntfs: device not mapped: %s\n", dev);
        return 1;
    }
    char path[512] = {0};
    strncpy(path, mapped, sizeof path - 1);

    mkfs_ntfs_opts_t opt = {
        .image_path     = path,
        .lba_offset     = 0,
        .bytes_per_sec  = 512,
        .sec_per_clus   = 8,   // 4KiB clusters on 512B sectors
        .mft_start_clus = 4,
        .mftmirr_clus   = 8,
        .verbose        = 0
    };

    for (int i=2; i<argc; i++){
        if (!strcmp(argv[i], "-o") && i+1 < argc) {
            opt.lba_offset = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-S") && i+1 < argc) {
            unsigned v = (unsigned)strtoul(argv[++i], NULL, 0);
            if (!is_pow2_u16(v) || (v < 512 || v > 4096)) {
                printf("mkfs.ntfs: -S must be 512, 1024, 2048, or 4096\n");
                return 2;
            }
            opt.bytes_per_sec = (uint16_t)v;
        } else if (!strcmp(argv[i], "-c") && i+1 < argc) {
            unsigned v = (unsigned)strtoul(argv[++i], NULL, 0);
            if (!is_pow2_u8(v)) {
                printf("mkfs.ntfs: -c (sectors/cluster) must be power of two <= 128\n");
                return 2;
            }
            opt.sec_per_clus = (uint8_t)v;
        } else if (!strcmp(argv[i], "--mft") && i+1 < argc) {
            unsigned long v = strtoul(argv[++i], NULL, 0);
            if (v == 0 || v > UINT_MAX) {
                printf("mkfs.ntfs: --mft LCN must be 1..%u\n", UINT_MAX);
                return 2;
            }
            opt.mft_start_clus = (uint32_t)v;
        } else if (!strcmp(argv[i], "--mftmirr") && i+1 < argc) {
            unsigned long v = strtoul(argv[++i], NULL, 0);
            if (v == 0 || v > UINT_MAX) {
                printf("mkfs.ntfs: --mftmirr LCN must be 1..%u\n", UINT_MAX);
                return 2;
            }
            opt.mftmirr_clus = (uint32_t)v;
        } else if (!strcmp(argv[i], "-v")) {
            opt.verbose = 1;
        } else {
            printf("mkfs.ntfs: bad or incomplete flag '%s'\n", argv[i]);
            return 2;
        }
    }

    if (opt.mft_start_clus == opt.mftmirr_clus) {
        printf("mkfs.ntfs: --mft and --mftmirr must be different clusters\n");
        return 2;
    }

    int rc = mkfs_ntfs_core(&opt);
    if (rc == 0)
        printf("mkfs.ntfs: initialized NTFS core on %s (%s)\n", dev, path);
    return rc;
}
