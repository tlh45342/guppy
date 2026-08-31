#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vblk.h"

#if defined(__GNUC__) || defined(__clang__)
  #define PACKED __attribute__((packed))
#else
  #pragma pack(push,1)
  #define PACKED
#endif

typedef struct PACKED {
    uint32_t s_inodes_count, s_blocks_count, s_r_blocks_count;
    uint32_t s_free_blocks_count, s_free_inodes_count, s_first_data_block;
    uint32_t s_log_block_size, s_log_frag_size, s_blocks_per_group;
    uint32_t s_frags_per_group, s_inodes_per_group;
} ext2_superblock;

typedef struct PACKED {
    uint32_t bg_block_bitmap, bg_inode_bitmap, bg_inode_table;
    uint16_t bg_free_blocks_count, bg_free_inodes_count, bg_used_dirs_count, bg_pad;
    uint32_t bg_reserved[3];
} ext2_group_desc;

typedef struct PACKED {
    uint16_t i_mode, i_uid;
    uint32_t i_size, i_atime, i_ctime, i_mtime, i_dtime;
    uint16_t i_gid, i_links_count;
    uint32_t i_blocks, i_flags, i_osd1, i_block[15];
    uint32_t i_generation, i_file_acl, i_dir_acl, i_faddr;
    uint8_t i_osd2[12];
} ext2_inode;

typedef struct PACKED {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len, file_type;
    char name[];
} ext2_dirent;

#if !defined(__GNUC__) && !defined(__clang__)
  #pragma pack(pop)
#endif
#undef PACKED

/* EXT2 operates on a filesystem-relative virtual block device. */
int mkfs_ext2_core(vblk_t *dev, uint64_t bytes, const char *label);
int ext2_unlink_at(vblk_t *dev, const char *path);
int ext2_truncate_at(vblk_t *dev, const char *path, uint64_t size);
int ext2_write_existing_at(vblk_t *dev, const char *path, const void *data, size_t len);
int ext2_create_and_write(vblk_t *dev, const char *path, const void *data, size_t len);
int ext2_mkdir_at(vblk_t *dev, const char *path, uint16_t mode);
