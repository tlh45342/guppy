// src/cmd_mkfs_ext2

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "cmds.h"
#include "devutil.h"
#include "diskio.h"
#include "gpt.h"
#include "devmap.h"

#pragma pack(push,1)
typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_cluster_size; // ext2 calls it log_frag_size; we keep name simple
    uint32_t blocks_per_group;
    uint32_t clusters_per_group; // frags_per_group
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;           // 0xEF53
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;

    // Extended (rev>=1)
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t  uuid[16];
    char     volume_name[16];
    char     last_mounted[64];
    uint32_t algo_bitmap;

    // ... (we can leave the rest zero)
} Ext2Super;
#pragma pack(pop)

// Superblock lives 1024 bytes from FS start for 1K block size.
// This first pass writes a minimal, not-complete superblock.
static int do_mkfs_ext2(const char *image_path, uint64_t fs_offset_bytes, uint64_t fs_bytes, const char *label) {
    (void)fs_bytes;

    // Zero first few blocks for clean slate (optional)
    {
        const size_t wipe = 4096;
        char zeros[4096] = {0};
        if (!file_pwrite(zeros, wipe, (size_t)fs_offset_bytes, image_path)) {
            fprintf(stderr, "mkfs.ext2: failed to zero header area\n");
            return 2;
        }
    }

    // Prepare superblock
    Ext2Super sb; memset(&sb, 0, sizeof(sb));
    sb.inodes_count       = 1024;     // placeholder
    sb.blocks_count       = 8192;     // placeholder
    sb.first_data_block   = 1;        // for 1KiB blocks, first_data_block=1
    sb.log_block_size     = 0;        // 0 => 1KiB blocks (good starter)
    sb.blocks_per_group   = 8192;     // placeholder
    sb.inodes_per_group   = 1024;     // placeholder
    sb.magic              = 0xEF53;
    sb.rev_level          = 1;
    sb.inode_size         = 128;      // classic ext2 inode size
    // volume label
    if (label) {
        size_t L = strlen(label);
        if (L > sizeof(sb.volume_name)) L = sizeof(sb.volume_name);
        memcpy(sb.volume_name, label, L);
    }

    // Write superblock at fs_offset + 1024
    size_t super_off = (size_t)(fs_offset_bytes + 1024);
    if (!file_pwrite(&sb, sizeof(sb), super_off, image_path)) {
        fprintf(stderr, "mkfs.ext2: failed to write superblock\n");
        return 2;
    }

    printf("mkfs.ext2: wrote minimal superblock (label=\"%s\"). NOTE: not fully populated yet.\n",
           label ? label : "");
    return 0;
}

static void print_help(void) {
    printf("mkfs.ext2 <dev> --part N [--label NAME]\n");
}

int cmd_mkfs_ext2(int argc, char **argv) {
    if (argc < 2) { print_help(); return 2; }

    const char *dev   = argv[1];
    int part_flag     = 0;
    const char *label = NULL;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--part") == 0 && i + 1 < argc) {
            part_flag = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc) {
            label = argv[++i];
        }
    }

    char base[32];
    int part_from_name = 0;
    if (!dev_split(dev, base, sizeof(base), &part_from_name)) {
        fprintf(stderr, "mkfs.ext2: '%s' is not a /dev/* device\n", dev);
        return 2;
    }

    const char *img = devmap_resolve(base);
    if (!img) {
        fprintf(stderr, "mkfs.ext2: unknown device %s (use -i <img> %s first)\n", base, base);
        return 2;
    }

    int final_part = part_flag ? part_flag : part_from_name;
    if (part_flag && part_from_name && part_flag != part_from_name) {
        fprintf(stderr, "mkfs.ext2: warning: device suffix (%d) != --part %d; using --part\n",
                part_from_name, part_flag);
    }
    if (final_part == 0) {
        fprintf(stderr, "mkfs.ext2: missing partition (use --part N or /dev/aN)\n");
        return 2;
    }

    uint64_t start_lba=0, total_sectors=0;
    if (!gpt_get_partition(img, final_part, &start_lba, &total_sectors)) {
        fprintf(stderr, "mkfs.ext2: failed to locate GPT partition %d on %s\n", final_part, img);
        return 2;
    }

    const uint64_t sector_size = 512;
    uint64_t fs_off  = start_lba * sector_size;
    uint64_t fs_size = total_sectors * sector_size;
    if (fs_size < 4096) {
        fprintf(stderr, "mkfs.ext2: partition too small\n");
        return 2;
    }

    return do_mkfs_ext2(img, fs_off, fs_size, label);
}
