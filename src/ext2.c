// --- BEGIN: minimal mkfs ext2 core ------------------------------------------
#include "diskio.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>

#ifndef LSEC
#define LSEC 512u
#endif

/* On-disk structures (ext2 classic, little-endian) */
#pragma pack(push,1)
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;     // block_size = 1024 << s_log_block_size
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;              // 0xEF53
    uint16_t s_state;              // 1 = clean
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;          // 1 = dynamic
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    // dynamic fields (rev >= 1)
    uint32_t s_first_ino;          // first non-reserved inode
    uint16_t s_inode_size;         // typically 128
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    // (we don’t need the rest for a minimal fs)
} ext2_superblock;

typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_group_desc;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;             // in 512-byte sectors
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];          // 12 direct, 1 ind, 1 dind, 1 tind
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;        // 2 = dir if filetype feature used; safe to set anyway
    char     name[];           // not NUL-terminated
} ext2_dirent;
#pragma pack(pop)

/* Helpers */
static bool pwrite_bytes_at(const char *key, uint64_t abs_off, const void *src, uint32_t len){
    return diskio_pwrite(key, abs_off, src, len);
}
static bool pwrite_block(const char *key, uint64_t fs_off, uint32_t block_size,
                         uint32_t block_index, const void *src, uint32_t len) {
    return pwrite_bytes_at(key, fs_off + (uint64_t)block_index * block_size, src, len);
}

static void set_bit(uint8_t *map, uint32_t idx) {
    map[idx >> 3] |= (uint8_t)(1u << (idx & 7u));
}

/* Public mkfs entry point used by cmd_mkfs_ext2.c */
int mkfs_ext2_core(const char *key, uint64_t off, uint64_t bytes, const char *label) {
    const uint32_t block_size = 1024u;         // keep it simple (1 KiB)
    if (bytes < 64 * 1024) {                   // arbitrary floor
        fprintf(stderr, "mkfs.ext2: device too small (%" PRIu64 " bytes)\n", bytes);
        return -1;
    }

    const uint32_t total_blocks = (uint32_t)(bytes / block_size);
    const uint32_t inode_size   = 128u;
    const uint32_t inodes_per_group = 128u;
    const uint32_t inode_tbl_blocks = (inodes_per_group * inode_size) / block_size; // 16 blocks
    const uint32_t first_data_block = 1;      // for 1 KiB block size

    /* Layout (single group):
       0:   boot/unused
       1:   superblock
       2:   group desc table
       3:   block bitmap
       4:   inode bitmap
       5..: inode table (16 blocks)
       data_start: first data block (root dir will use 1 block here)
    */
    const uint32_t sb_blk   = 1;
    const uint32_t gdt_blk  = 2;
    const uint32_t bb_blk   = 3;
    const uint32_t ib_blk   = 4;
    const uint32_t it_blk   = 5;
    const uint32_t data_start_blk = it_blk + inode_tbl_blocks;

    if (total_blocks <= data_start_blk + 1) {
        fprintf(stderr, "mkfs.ext2: device too small for minimal layout\n");
        return -1;
    }

    /* Zero the first few dozen blocks to start clean */
    {
        uint8_t zero[1024]; memset(zero, 0, sizeof zero);
        uint32_t zero_upto = data_start_blk + 8;  // some headroom
        if (zero_upto > total_blocks) zero_upto = total_blocks;
        for (uint32_t b = 0; b < zero_upto; ++b) {
            if (!pwrite_block(key, off, block_size, b, zero, sizeof zero))
                return -1;
        }
    }

    /* --- Superblock --- */
    ext2_superblock sb; memset(&sb, 0, sizeof sb);
    sb.s_inodes_count      = inodes_per_group;   // single group
    sb.s_blocks_count      = total_blocks;
    sb.s_r_blocks_count    = 0;
    // We'll mark metadata + one data block used:
    const uint32_t used_blocks = data_start_blk + 1; // through inode table + 1 data block (root)
    sb.s_free_blocks_count  = (total_blocks > used_blocks) ? (total_blocks - used_blocks) : 0;
    // Reserve inodes 1..10 per ext2 convention; allocate root (2)
    const uint32_t reserved_inodes = 10;
    sb.s_free_inodes_count = (inodes_per_group > reserved_inodes) ? (inodes_per_group - reserved_inodes) : 0;

    sb.s_first_data_block   = first_data_block;
    sb.s_log_block_size     = 0;                 // 1 KiB
    sb.s_log_frag_size      = 0;                 // 1 KiB
    sb.s_blocks_per_group   = total_blocks;
    sb.s_frags_per_group    = total_blocks;
    sb.s_inodes_per_group   = inodes_per_group;
    sb.s_mtime              = (uint32_t)time(NULL);
    sb.s_wtime              = sb.s_mtime;
    sb.s_mnt_count          = 0;
    sb.s_max_mnt_count      = 20;
    sb.s_magic              = 0xEF53;
    sb.s_state              = 1;                 // clean
    sb.s_errors             = 1;                 // continue
    sb.s_minor_rev_level    = 0;
    sb.s_lastcheck          = sb.s_mtime;
    sb.s_checkinterval      = 0;
    sb.s_creator_os         = 0;                 // Linux
    sb.s_rev_level          = 1;                 // dynamic
    sb.s_first_ino          = 11;                // first non-reserved inode
    sb.s_inode_size         = inode_size;
    sb.s_block_group_nr     = 0;
    sb.s_feature_compat     = 0;
    sb.s_feature_incompat   = 0;
    sb.s_feature_ro_compat  = 0;
    if (label) {
        snprintf(sb.s_volume_name, sizeof sb.s_volume_name, "%s", label);
    }

    // Place superblock at offset 1024 (block 1)
    if (!pwrite_bytes_at(key, off + (uint64_t)sb_blk * block_size, &sb, sizeof sb))
        return -1;

    /* --- Group Descriptor Table (1 entry) --- */
    ext2_group_desc gd; memset(&gd, 0, sizeof gd);
    gd.bg_block_bitmap      = bb_blk;
    gd.bg_inode_bitmap      = ib_blk;
    gd.bg_inode_table       = it_blk;
    gd.bg_free_blocks_count = (uint16_t)sb.s_free_blocks_count;
    gd.bg_free_inodes_count = (uint16_t)sb.s_free_inodes_count;
    gd.bg_used_dirs_count   = 1;    // root

    if (!pwrite_block(key, off, block_size, gdt_blk, &gd, sizeof gd))
        return -1;

    /* --- Block Bitmap --- */
    uint8_t bb[1024]; memset(bb, 0, sizeof bb);
    for (uint32_t b = 0; b <= data_start_blk; ++b) set_bit(bb, b); // metadata + root data block
    if (!pwrite_block(key, off, block_size, bb_blk, bb, sizeof bb))
        return -1;

    /* --- Inode Bitmap --- */
    uint8_t ib[1024]; memset(ib, 0, sizeof ib);
    // Mark inodes 1..10 as reserved/used; inode numbers are 1-based.
    for (uint32_t ino = 1; ino <= 10; ++ino) set_bit(ib, ino - 1);
    if (!pwrite_block(key, off, block_size, ib_blk, ib, sizeof ib))
        return -1;

    /* --- Inode Table --- */
    // We'll only initialize inode #2 (root). Others left zero (free).
    // inode numbers are 1-based; in inode table, index 0 == inode 1.
    uint8_t it_block[1024]; memset(it_block, 0, sizeof it_block);
    const uint32_t root_data_block = data_start_blk;  // use first data block

    // Compose inode #2
    ext2_inode root; memset(&root, 0, sizeof root);
    root.i_mode        = 040755;    // dir
    root.i_links_count = 2;
    root.i_uid         = 0;
    root.i_gid         = 0;
    root.i_size        = block_size;
    root.i_atime = root.i_ctime = root.i_mtime = (uint32_t)time(NULL);
    root.i_blocks      = (block_size / 512);     // 2 sectors for 1 KiB block
    root.i_block[0]    = root_data_block;

    // Write inode #2 to inode table (find its position)
    const uint32_t inodes_per_block = block_size / sizeof(ext2_inode); // 8 at 1KiB
    const uint32_t root_index = 2 - 1;          // 1-based -> 0-based
    const uint32_t root_tbl_rel_blk = root_index / inodes_per_block;  // 0
    const uint32_t root_tbl_off     = (root_index % inodes_per_block) * sizeof(ext2_inode);

    // Write the first inode-table block with root inode populated
    memcpy(it_block + root_tbl_off, &root, sizeof root);
    if (!pwrite_block(key, off, block_size, it_blk + root_tbl_rel_blk, it_block, sizeof it_block))
        return -1;

    // Zero the rest of inode table blocks (already zeroed earlier, but ensure)
    uint8_t zero[1024]; memset(zero, 0, sizeof zero);
    for (uint32_t k = 1; k < inode_tbl_blocks; ++k) {
        if (!pwrite_block(key, off, block_size, it_blk + k, zero, sizeof zero))
            return -1;
    }

    /* --- Root directory block --- */
    uint8_t dirblk[1024]; memset(dirblk, 0, sizeof dirblk);

    // '.' entry
    {
        ext2_dirent *de = (ext2_dirent*)dirblk;
        de->inode    = 2;
        de->name_len = 1;
        de->file_type= 2;      // dir
        de->rec_len  = 12;     // 8 + 1 name -> aligned to 4
        de->name[0]  = '.';
    }

    // '..' entry (fills the rest of the block)
    {
        ext2_dirent *de2 = (ext2_dirent*)(dirblk + 12);
        de2->inode    = 2;     // parent of root is itself
        de2->name_len = 2;
        de2->file_type= 2;     // dir
        de2->rec_len  = (uint16_t)(block_size - 12);
        de2->name[0]  = '.';
        de2->name[1]  = '.';
    }

    if (!pwrite_block(key, off, block_size, root_data_block, dirblk, sizeof dirblk))
        return -1;

    // Success
    return 0;
}
// --- END: minimal mkfs ext2 core --------------------------------------------

/* Shared structs/helpers from mkfs section are reused:
   - ext2_superblock, ext2_group_desc, ext2_inode, ext2_dirent
   - pwrite_bytes_at, pwrite_block
   Add this read helper:
*/
static bool pread_bytes_at(const char *key, uint64_t abs_off, void *dst, uint32_t len){
    return diskio_pread(key, abs_off, dst, len);
}
static bool pread_block(const char *key, uint64_t fs_off, uint32_t block_size,
                        uint32_t block_index, void *dst, uint32_t len){
    return pread_bytes_at(key, fs_off + (uint64_t)block_index * block_size, dst, len);
}

static inline uint16_t dirent_min_rec_len(uint8_t name_len){
    uint16_t n = (uint16_t)(8 + name_len);
    return (uint16_t)((n + 3u) & ~3u);
}

/* Signature NOTE:
   If your header declares a different prototype (arg order/count), change this
   signature to match it. Commonly it’s:
     int ext2_create_and_write(const char *key, uint64_t off,
                               const char *path, const void *data, size_t len);
*/
int ext2_create_and_write(const char *key, uint64_t off,
                          const char *path, const void *data, size_t len)
{
    if (!key || !path || !data) return -1;
    if (path[0] == '/') path++;            // accept "/name" or "name"

    // reject nested paths for now (root only)
    for (const char *p = path; *p; ++p) if (*p == '/') return -2;
    uint8_t name_len = (uint8_t)strlen(path);
    if (name_len == 0 || name_len > 60) return -3;

    /* Read superblock */
    ext2_superblock sb;
    if (!pread_bytes_at(key, off + 1024, &sb, sizeof sb)) return -4;
    if (sb.s_magic != 0xEF53) return -5;

    const uint32_t block_size = 1024u << sb.s_log_block_size;
    const uint32_t sb_blk     = (block_size == 1024u) ? 1u : 0u;  // classic placement
    const uint32_t gdt_blk    = sb_blk + 1u;

    /* Read group descriptor (single group) */
    ext2_group_desc gd;
    if (!pread_block(key, off, block_size, gdt_blk, &gd, sizeof gd)) return -6;
    const uint32_t bb_blk = gd.bg_block_bitmap;
    const uint32_t ib_blk = gd.bg_inode_bitmap;
    const uint32_t it_blk = gd.bg_inode_table;

    /* Load bitmaps (support up to 4 KiB blocks here) */
    uint8_t bb[4096], ib[4096];
    if (block_size > sizeof bb || block_size > sizeof ib) return -7;
    if (!pread_block(key, off, block_size, bb_blk, bb, block_size)) return -8;
    if (!pread_block(key, off, block_size, ib_blk, ib, block_size)) return -9;

    /* Find a free inode (start at first non-reserved) */
    const uint32_t first_ino = (sb.s_rev_level >= 1 && sb.s_first_ino >= 11) ? sb.s_first_ino : 11u;
    uint32_t free_ino = 0;
    for (uint32_t ino = first_ino; ino <= sb.s_inodes_per_group; ++ino) {
        const uint32_t idx = ino - 1u;
        if ((ib[idx >> 3] & (uint8_t)(1u << (idx & 7u))) == 0) { free_ino = ino; break; }
    }
    if (!free_ino) return -10;

    /* Find a free data block (skip metadata; start after inode table) */
    const uint32_t inode_tbl_blocks = (sb.s_inodes_per_group * sb.s_inode_size) / block_size;
    const uint32_t data_start_blk   = it_blk + inode_tbl_blocks;
    uint32_t free_blk = 0;
    for (uint32_t b = data_start_blk; b < sb.s_blocks_per_group; ++b) {
        if ((bb[b >> 3] & (uint8_t)(1u << (b & 7u))) == 0) { free_blk = b; break; }
    }
    if (!free_blk) return -11;

    /* Truncate write to one block (simple case) */
    uint32_t wlen = (len > block_size) ? block_size : (uint32_t)len;

    /* Write file data */
    if (!pwrite_block(key, off, block_size, free_blk, data, wlen)) return -12;

    /* Create inode for the new file */
    ext2_inode file; memset(&file, 0, sizeof file);
    file.i_mode        = 0100644;       // regular file
    file.i_links_count = 1;
    file.i_uid         = 0;
    file.i_gid         = 0;
    file.i_size        = wlen;
    file.i_atime = file.i_ctime = file.i_mtime = (uint32_t)time(NULL);
    file.i_blocks      = (uint32_t)((wlen + 511u) / 512u);   // sectors
    if (file.i_blocks == 0) file.i_blocks = 2;               // minimum accounting
    file.i_block[0]    = free_blk;

    /* Write inode into inode table */
    const uint32_t inodes_per_block = block_size / sb.s_inode_size;
    const uint32_t idx0             = free_ino - 1u;                      // 0-based
    const uint32_t tbl_rel_blk      = idx0 / inodes_per_block;
    const uint32_t tbl_off          = (idx0 % inodes_per_block) * sb.s_inode_size;

    uint8_t itbuf[4096];
    if (block_size > sizeof itbuf) return -13;
    if (!pread_block(key, off, block_size, it_blk + tbl_rel_blk, itbuf, block_size)) return -13;
    memcpy(itbuf + tbl_off, &file, sizeof file);
    if (!pwrite_block(key, off, block_size, it_blk + tbl_rel_blk, itbuf, block_size)) return -14;

    /* Update inode bitmap */
    ib[idx0 >> 3] |= (uint8_t)(1u << (idx0 & 7u));
    if (!pwrite_block(key, off, block_size, ib_blk, ib, block_size)) return -15;

    /* Update block bitmap */
    bb[free_blk >> 3] |= (uint8_t)(1u << (free_blk & 7u));
    if (!pwrite_block(key, off, block_size, bb_blk, bb, block_size)) return -16;

    /* Update superblock counts */
    if (sb.s_free_inodes_count) sb.s_free_inodes_count--;
    if (sb.s_free_blocks_count) sb.s_free_blocks_count--;
    if (!pwrite_bytes_at(key, off + 1024, &sb, sizeof sb)) return -17;

    /* Append directory entry into root directory block */
    // Load root inode (#2) and its data block
    ext2_inode root;
    const uint32_t root_index       = 1u; // inode 2 -> 0-based index 1
    const uint32_t root_tbl_rel_blk = root_index / inodes_per_block;
    const uint32_t root_tbl_off     = (root_index % inodes_per_block) * sb.s_inode_size;
    if (!pread_block(key, off, block_size, it_blk + root_tbl_rel_blk, itbuf, block_size)) return -18;
    memcpy(&root, itbuf + root_tbl_off, sizeof root);
    if (root.i_block[0] == 0) return -19;

    uint8_t dirblk[4096];
    if (block_size > sizeof dirblk) return -20;
    if (!pread_block(key, off, block_size, root.i_block[0], dirblk, block_size)) return -20;

    // Scan existing entries to adjust last one's rec_len and append ours
    uint32_t pos = 0;
    while (pos + 8u <= block_size) {
        ext2_dirent *de = (ext2_dirent*)(dirblk + pos);
        if (de->rec_len == 0) break;
        if (pos + (uint32_t)de->rec_len > block_size) break; // corrupt guard

        const uint16_t minimal = dirent_min_rec_len(de->name_len);
        if (pos + (uint32_t)de->rec_len >= block_size) {
            // Last entry in block: shrink to minimal and append our new entry
            const uint32_t new_pos = pos + (uint32_t)minimal;
            const uint32_t need    = (uint32_t)dirent_min_rec_len(name_len);
            if (new_pos + need > block_size) return -21; // no space
            de->rec_len = minimal;

            ext2_dirent *ne = (ext2_dirent*)(dirblk + new_pos);
            ne->inode     = free_ino;
            ne->name_len  = name_len;
            ne->file_type = 1; // regular file
            ne->rec_len   = (uint16_t)(block_size - new_pos);
            memcpy(ne->name, path, name_len);

            if (!pwrite_block(key, off, block_size, root.i_block[0], dirblk, block_size)) return -22;
            return 0;
        }
        pos += (uint32_t)de->rec_len;
    }
    return -23; // couldn't append
}