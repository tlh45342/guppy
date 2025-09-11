// include/ext2.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ---- packing helper ---- */
#if defined(__GNUC__) || defined(__clang__)
  #define PACKED __attribute__((packed))
#else
  #pragma pack(push,1)
  #define PACKED
#endif

/* ---- superblock (only fields we use) ---- */
typedef struct PACKED {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;      // block_size = 1024 << s_log_block_size
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    /* ... many more fields in real ext2; omitted for now ... */
} ext2_superblock;

/* ---- group descriptor (only fields we use) ---- */
typedef struct PACKED {
    uint32_t bg_block_bitmap;   // block number of block bitmap
    uint32_t bg_inode_bitmap;   // block number of inode bitmap
    uint32_t bg_inode_table;    // starting block of inode table
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} ext2_group_desc;

/* ---- inode (only fields we use) ---- */
typedef struct PACKED {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];   // direct[0..11], single, double, triple
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode;

/* ---- directory entry (ext2 “version 2” with file_type) ---- */
typedef struct PACKED {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;   // 1=file, 2=dir (etc.)
    char     name[];      // not NUL-terminated
} ext2_dirent;

#if !defined(__GNUC__) && !defined(__clang__)
  #pragma pack(pop)
#endif
#undef PACKED

/* ---- public API you’re exposing from ext2.c ---- */
bool ext2_create_and_write(const char *path, const uint8_t *data, uint32_t len);

/* If other TUs need them, you can also declare readers/writers here:
bool ext2_read_super(ext2_superblock *out);
bool ext2_read_inode(uint32_t ino, ext2_inode *out);
*/
