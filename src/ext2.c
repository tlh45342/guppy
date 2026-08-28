// --- BEGIN: minimal mkfs ext2 core ------------------------------------------
#include "diskio.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

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
    /*
     * Use 4 KiB blocks for Guppy's current single-group formatter.
     * One EXT2 block bitmap can describe block_size * 8 blocks, so a 4 KiB
     * bitmap covers 32768 blocks (128 MiB).  The previous 1 KiB formatter
     * incorrectly advertised >8192 blocks in one group on larger images.
     */
    const uint32_t block_size = 4096u;
    if (bytes < 64 * 1024) {                   // arbitrary floor
        fprintf(stderr, "mkfs.ext2: device too small (%" PRIu64 " bytes)\n", bytes);
        return -1;
    }

    const uint32_t total_blocks = (uint32_t)(bytes / block_size);
    const uint32_t inode_size   = 128u;
    const uint32_t inodes_per_group = 128u;
    const uint32_t inode_tbl_blocks =
        (inodes_per_group * inode_size + block_size - 1u) / block_size;
    const uint32_t first_data_block = 0;

    /* 4 KiB, one group:
       block 0: superblock lives at byte offset 1024 within this block
       block 1: group descriptor table
       block 2: block bitmap
       block 3: inode bitmap
       block 4..: inode table
       data_start: first data block (root directory)
    */
    const uint32_t gdt_blk  = 1;
    const uint32_t bb_blk   = 2;
    const uint32_t ib_blk   = 3;
    const uint32_t it_blk   = 4;
    const uint32_t data_start_blk = it_blk + inode_tbl_blocks;

    if (total_blocks <= data_start_blk + 1) {
        fprintf(stderr, "mkfs.ext2: device too small for minimal layout\n");
        return -1;
    }

    /* Zero the first few dozen blocks to start clean */
    {
        uint8_t zero[4096]; memset(zero, 0, sizeof zero);
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
    sb.s_log_block_size     = 2;                 // 4 KiB
    sb.s_log_frag_size      = 2;                 // 4 KiB
    if (total_blocks > block_size * 8u) {
        fprintf(stderr,
                "mkfs.ext2: current formatter supports up to %u MiB per filesystem\n",
                (block_size * 8u * block_size) / (1024u * 1024u));
        return -1;
    }
    sb.s_blocks_per_group   = block_size * 8u;
    sb.s_frags_per_group    = block_size * 8u;
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

    // EXT2 superblock is always at byte offset 1024.
    if (!pwrite_bytes_at(key, off + 1024u, &sb, sizeof sb))
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
    uint8_t bb[4096]; memset(bb, 0, sizeof bb);
    for (uint32_t b = 0; b <= data_start_blk; ++b) set_bit(bb, b); // metadata + root data block
    if (!pwrite_block(key, off, block_size, bb_blk, bb, sizeof bb))
        return -1;

    /* --- Inode Bitmap --- */
    uint8_t ib[4096]; memset(ib, 0, sizeof ib);
    // Mark inodes 1..10 as reserved/used; inode numbers are 1-based.
    for (uint32_t ino = 1; ino <= 10; ++ino) set_bit(ib, ino - 1);
    if (!pwrite_block(key, off, block_size, ib_blk, ib, sizeof ib))
        return -1;

    /* --- Inode Table --- */
    // We'll only initialize inode #2 (root). Others left zero (free).
    // inode numbers are 1-based; in inode table, index 0 == inode 1.
    uint8_t it_block[4096]; memset(it_block, 0, sizeof it_block);
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
    const uint32_t inodes_per_block = block_size / inode_size;
    const uint32_t root_index = 2 - 1;          // 1-based -> 0-based
    const uint32_t root_tbl_rel_blk = root_index / inodes_per_block;  // 0
    const uint32_t root_tbl_off     = (root_index % inodes_per_block) * sizeof(ext2_inode);

    // Write the first inode-table block with root inode populated
    memcpy(it_block + root_tbl_off, &root, sizeof root);
    if (!pwrite_block(key, off, block_size, it_blk + root_tbl_rel_blk, it_block, sizeof it_block))
        return -1;

    // Zero the rest of inode table blocks (already zeroed earlier, but ensure)
    uint8_t zero[4096]; memset(zero, 0, sizeof zero);
    for (uint32_t k = 1; k < inode_tbl_blocks; ++k) {
        if (!pwrite_block(key, off, block_size, it_blk + k, zero, sizeof zero))
            return -1;
    }

    /* --- Root directory block --- */
    uint8_t dirblk[4096]; memset(dirblk, 0, sizeof dirblk);

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

typedef struct {
    ext2_superblock sb;
    ext2_group_desc gd;
    uint32_t block_size;
    uint32_t sb_blk;
    uint32_t gdt_blk;
    uint32_t bb_blk;
    uint32_t ib_blk;
    uint32_t it_blk;
    uint32_t inode_tbl_blocks;
    uint32_t data_start_blk;
} ext2_meta;

static int ext2_load_meta(const char *key, uint64_t off, ext2_meta *m) {
    if (!key || !m) return -1;
    memset(m, 0, sizeof *m);
    if (!pread_bytes_at(key, off + 1024, &m->sb, sizeof m->sb)) return -2;
    if (m->sb.s_magic != 0xEF53) return -3;
    m->block_size = 1024u << m->sb.s_log_block_size;
    if (m->block_size == 0 || m->block_size > 4096u) return -4;
    m->sb_blk = (m->block_size == 1024u) ? 1u : 0u;
    m->gdt_blk = m->sb_blk + 1u;
    if (!pread_block(key, off, m->block_size, m->gdt_blk, &m->gd, sizeof m->gd)) return -5;
    m->bb_blk = m->gd.bg_block_bitmap;
    m->ib_blk = m->gd.bg_inode_bitmap;
    m->it_blk = m->gd.bg_inode_table;
    if (m->sb.s_inode_size == 0) m->sb.s_inode_size = 128;
    m->inode_tbl_blocks = (m->sb.s_inodes_per_group * m->sb.s_inode_size + m->block_size - 1u) / m->block_size;
    m->data_start_blk = m->it_blk + m->inode_tbl_blocks;
    return 0;
}

static int ext2_read_inode_at(const char *key, uint64_t off, const ext2_meta *m,
                              uint32_t ino, ext2_inode *out) {
    if (!key || !m || !out || ino == 0 || ino > m->sb.s_inodes_per_group) return -1;
    const uint32_t inodes_per_block = m->block_size / m->sb.s_inode_size;
    if (inodes_per_block == 0) return -2;
    const uint32_t idx0 = ino - 1u;
    const uint32_t tbl_rel_blk = idx0 / inodes_per_block;
    const uint32_t tbl_off = (idx0 % inodes_per_block) * m->sb.s_inode_size;
    uint8_t buf[4096];
    if (m->block_size > sizeof buf) return -3;
    if (!pread_block(key, off, m->block_size, m->it_blk + tbl_rel_blk, buf, m->block_size)) return -4;
    memset(out, 0, sizeof *out);
    memcpy(out, buf + tbl_off, sizeof *out);
    return 0;
}

static int ext2_write_inode_at(const char *key, uint64_t off, const ext2_meta *m,
                               uint32_t ino, const ext2_inode *in) {
    if (!key || !m || !in || ino == 0 || ino > m->sb.s_inodes_per_group) return -1;
    const uint32_t inodes_per_block = m->block_size / m->sb.s_inode_size;
    if (inodes_per_block == 0) return -2;
    const uint32_t idx0 = ino - 1u;
    const uint32_t tbl_rel_blk = idx0 / inodes_per_block;
    const uint32_t tbl_off = (idx0 % inodes_per_block) * m->sb.s_inode_size;
    uint8_t buf[4096];
    if (m->block_size > sizeof buf) return -3;
    if (!pread_block(key, off, m->block_size, m->it_blk + tbl_rel_blk, buf, m->block_size)) return -4;
    memcpy(buf + tbl_off, in, sizeof *in);
    if (!pwrite_block(key, off, m->block_size, m->it_blk + tbl_rel_blk, buf, m->block_size)) return -5;
    return 0;
}

static int ext2_find_entry_at(const char *key, uint64_t off, const ext2_meta *m,
                              uint32_t dir_ino, const char *name,
                              uint32_t *ino_out, uint8_t *type_out) {
    if (!key || !m || !name || !*name) return -1;
    ext2_inode dir;
    if (ext2_read_inode_at(key, off, m, dir_ino, &dir) != 0) return -2;
    if ((dir.i_mode & 0170000) != 0040000) return -3;
    size_t want = strlen(name);
    if (want > 255u) return -4;
    uint8_t blk[4096];
    if (m->block_size > sizeof blk) return -5;
    for (int bi = 0; bi < 12 && dir.i_block[bi]; ++bi) {
        if (!pread_block(key, off, m->block_size, dir.i_block[bi], blk, m->block_size)) return -6;
        uint32_t pos = 0;
        while (pos + 8u <= m->block_size) {
            ext2_dirent *de = (ext2_dirent*)(blk + pos);
            if (de->rec_len < 8u || pos + de->rec_len > m->block_size) break;
            if (de->inode && de->name_len == want &&
                memcmp(de->name, name, want) == 0) {
                if (ino_out) *ino_out = de->inode;
                if (type_out) *type_out = de->file_type;
                return 1;
            }
            pos += de->rec_len;
        }
    }
    return 0;
}

static int ext2_resolve_path_at(const char *key, uint64_t off, const ext2_meta *m,
                                const char *path, uint32_t *ino_out) {
    if (!key || !m || !path || !ino_out) return -1;
    while (*path == '/') path++;
    if (*path == '\0') { *ino_out = 2; return 0; }

    char tmp[512];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return -2;
    memcpy(tmp, path, n + 1);

    uint32_t cur = 2;
    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *slash = strchr(p, '/');
        if (slash) *slash = '\0';
        uint32_t next = 0;
        int frc = ext2_find_entry_at(key, off, m, cur, p, &next, NULL);
        if (frc != 1) return -3;
        cur = next;
        if (!slash) break;
        p = slash + 1;
    }
    *ino_out = cur;
    return 0;
}

static int split_parent_name(const char *path, char *parent, size_t pcap,
                             char *name, size_t ncap) {
    if (!path || !parent || !name || pcap == 0 || ncap == 0) return -1;
    while (*path == '/') path++;
    size_t len = strlen(path);
    while (len && path[len - 1] == '/') len--;
    if (len == 0 || len >= 512u) return -2;
    char tmp[512];
    memcpy(tmp, path, len);
    tmp[len] = '\0';
    char *slash = strrchr(tmp, '/');
    const char *base = tmp;
    if (slash) {
        *slash = '\0';
        base = slash + 1;
        snprintf(parent, pcap, "%s", tmp);
    } else {
        parent[0] = '\0';
    }
    if (!*base || strlen(base) > 255u) return -3;
    snprintf(name, ncap, "%s", base);
    return 0;
}

static uint32_t find_free_inode(const ext2_meta *m, const uint8_t *ib) {
    const uint32_t first_ino = (m->sb.s_rev_level >= 1 && m->sb.s_first_ino >= 11) ? m->sb.s_first_ino : 11u;
    for (uint32_t ino = first_ino; ino <= m->sb.s_inodes_per_group; ++ino) {
        uint32_t idx = ino - 1u;
        if ((ib[idx >> 3] & (uint8_t)(1u << (idx & 7u))) == 0) return ino;
    }
    return 0;
}

static uint32_t find_free_block(const ext2_meta *m, const uint8_t *bb) {
    uint32_t limit = m->sb.s_blocks_count;
    if (m->sb.s_blocks_per_group && m->sb.s_blocks_per_group < limit) limit = m->sb.s_blocks_per_group;
    for (uint32_t b = m->data_start_blk; b < limit; ++b) {
        if ((bb[b >> 3] & (uint8_t)(1u << (b & 7u))) == 0) return b;
    }
    return 0;
}

/* Insert a directory entry. If all existing direct blocks are full, consume
   one additional free block from *extra_block (caller marks it allocated). */
static int ext2_append_dirent_at(const char *key, uint64_t off, const ext2_meta *m,
                                 uint32_t dir_ino, uint32_t child_ino,
                                 uint8_t file_type, const char *name,
                                 uint32_t *extra_block) {
    ext2_inode dir;
    if (ext2_read_inode_at(key, off, m, dir_ino, &dir) != 0) return -1;
    if ((dir.i_mode & 0170000) != 0040000) return -2;
    uint8_t nl = (uint8_t)strlen(name);
    uint16_t need = dirent_min_rec_len(nl);
    uint8_t blk[4096];
    if (m->block_size > sizeof blk) return -3;

    for (int bi = 0; bi < 12 && dir.i_block[bi]; ++bi) {
        if (!pread_block(key, off, m->block_size, dir.i_block[bi], blk, m->block_size)) return -4;
        uint32_t pos = 0;
        while (pos + 8u <= m->block_size) {
            ext2_dirent *de = (ext2_dirent*)(blk + pos);
            if (de->rec_len < 8u || pos + de->rec_len > m->block_size) break;
            uint16_t min = dirent_min_rec_len(de->name_len);
            if (de->rec_len >= min + need) {
                uint16_t oldrec = de->rec_len;
                de->rec_len = min;
                uint32_t newpos = pos + min;
                ext2_dirent *ne = (ext2_dirent*)(blk + newpos);
                memset(ne, 0, oldrec - min);
                ne->inode = child_ino;
                ne->rec_len = (uint16_t)(oldrec - min);
                ne->name_len = nl;
                ne->file_type = file_type;
                memcpy(ne->name, name, nl);
                if (!pwrite_block(key, off, m->block_size, dir.i_block[bi], blk, m->block_size)) return -5;
                dir.i_mtime = dir.i_ctime = (uint32_t)time(NULL);
                return ext2_write_inode_at(key, off, m, dir_ino, &dir);
            }
            pos += de->rec_len;
        }
    }

    int slot = -1;
    for (int bi = 0; bi < 12; ++bi) if (dir.i_block[bi] == 0) { slot = bi; break; }
    if (slot < 0 || !extra_block || *extra_block == 0) return -6;
    memset(blk, 0, m->block_size);
    ext2_dirent *ne = (ext2_dirent*)blk;
    ne->inode = child_ino;
    ne->rec_len = (uint16_t)m->block_size;
    ne->name_len = nl;
    ne->file_type = file_type;
    memcpy(ne->name, name, nl);
    if (!pwrite_block(key, off, m->block_size, *extra_block, blk, m->block_size)) return -7;
    dir.i_block[slot] = *extra_block;
    dir.i_size += m->block_size;
    dir.i_blocks += m->block_size / 512u;
    dir.i_mtime = dir.i_ctime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, m, dir_ino, &dir) != 0) return -8;
    return 1; /* caller must account for extra block */
}

static int ext2_store_alloc_state(const char *key, uint64_t off, ext2_meta *m,
                                  const uint8_t *bb, const uint8_t *ib) {
    if (!pwrite_block(key, off, m->block_size, m->bb_blk, bb, m->block_size)) return -1;
    if (!pwrite_block(key, off, m->block_size, m->ib_blk, ib, m->block_size)) return -2;
    if (!pwrite_bytes_at(key, off + 1024, &m->sb, sizeof m->sb)) return -3;
    if (!pwrite_block(key, off, m->block_size, m->gdt_blk, &m->gd, sizeof m->gd)) return -4;
    return 0;
}

int ext2_mkdir_at(const char *key, uint64_t off, const char *path, uint16_t mode) {
    ext2_meta m;
    int rc = ext2_load_meta(key, off, &m);
    if (rc != 0) return -10 + rc;

    char parent[512], name[256];
    if (split_parent_name(path, parent, sizeof parent, name, sizeof name) != 0) return -20;
    uint32_t parent_ino = 0;
    if (ext2_resolve_path_at(key, off, &m, parent, &parent_ino) != 0) return -21;

    uint32_t existing = 0;
    int exists = ext2_find_entry_at(key, off, &m, parent_ino, name, &existing, NULL);
    if (exists == 1) {
        ext2_inode ei;
        if (ext2_read_inode_at(key, off, &m, existing, &ei) == 0 && (ei.i_mode & 0170000) == 0040000) return 0;
        return -22;
    }
    if (exists < 0) return -23;

    uint8_t bb[4096], ib[4096];
    if (m.block_size > sizeof bb) return -24;
    if (!pread_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) return -25;
    if (!pread_block(key, off, m.block_size, m.ib_blk, ib, m.block_size)) return -26;
    uint32_t ino = find_free_inode(&m, ib);
    uint32_t data_blk = find_free_block(&m, bb);
    if (!ino || !data_blk) return -27;

    /* Reserve the child resources in-memory first. */
    uint32_t iidx = ino - 1u;
    ib[iidx >> 3] |= (uint8_t)(1u << (iidx & 7u));
    bb[data_blk >> 3] |= (uint8_t)(1u << (data_blk & 7u));

    ext2_inode child; memset(&child, 0, sizeof child);
    child.i_mode = (uint16_t)(0040000 | (mode ? (mode & 0777u) : 0755u));
    child.i_uid = child.i_gid = 0;
    child.i_size = m.block_size;
    child.i_atime = child.i_ctime = child.i_mtime = (uint32_t)time(NULL);
    child.i_links_count = 2;
    child.i_blocks = m.block_size / 512u;
    child.i_block[0] = data_blk;
    if (ext2_write_inode_at(key, off, &m, ino, &child) != 0) return -28;

    uint8_t dirblk[4096]; memset(dirblk, 0, m.block_size);
    ext2_dirent *dot = (ext2_dirent*)dirblk;
    dot->inode = ino; dot->rec_len = 12; dot->name_len = 1; dot->file_type = 2; dot->name[0] = '.';
    ext2_dirent *dotdot = (ext2_dirent*)(dirblk + 12);
    dotdot->inode = parent_ino; dotdot->rec_len = (uint16_t)(m.block_size - 12u);
    dotdot->name_len = 2; dotdot->file_type = 2; dotdot->name[0] = '.'; dotdot->name[1] = '.';
    if (!pwrite_block(key, off, m.block_size, data_blk, dirblk, m.block_size)) return -29;

    /* If parent needs a new directory block, choose another free block. */
    uint32_t extra = find_free_block(&m, bb);
    int arc = ext2_append_dirent_at(key, off, &m, parent_ino, ino, 2, name, &extra);
    if (arc < 0) return -30;
    uint32_t blocks_used = 1;
    if (arc == 1) {
        bb[extra >> 3] |= (uint8_t)(1u << (extra & 7u));
        blocks_used++;
    }

    ext2_inode parent_inode;
    if (ext2_read_inode_at(key, off, &m, parent_ino, &parent_inode) != 0) return -31;
    parent_inode.i_links_count++;
    parent_inode.i_mtime = parent_inode.i_ctime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, &m, parent_ino, &parent_inode) != 0) return -32;

    if (m.sb.s_free_inodes_count) m.sb.s_free_inodes_count--;
    if (m.gd.bg_free_inodes_count) m.gd.bg_free_inodes_count--;
    if (m.sb.s_free_blocks_count >= blocks_used) m.sb.s_free_blocks_count -= blocks_used;
    if (m.gd.bg_free_blocks_count >= blocks_used) m.gd.bg_free_blocks_count -= (uint16_t)blocks_used;
    m.gd.bg_used_dirs_count++;
    return ext2_store_alloc_state(key, off, &m, bb, ib);
}


/* Remove a regular file from this single-group EXT2 image.
   Frees direct, single-indirect, and double-indirect data/metadata blocks,
   clears the inode bitmap, removes the parent directory entry, and repairs
   free-space counters. Directories are deliberately rejected; rmdir is a
   separate operation. */
int ext2_unlink_at(const char *key, uint64_t off, const char *path)
{
    ext2_meta m;
    if (!key || !path) return -1;
    if (ext2_load_meta(key, off, &m) != 0) return -2;

    char parent[512], name[256];
    if (split_parent_name(path, parent, sizeof parent, name, sizeof name) != 0)
        return -3;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -4;

    uint32_t parent_ino = 0, ino = 0;
    uint8_t type = 0;
    if (ext2_resolve_path_at(key, off, &m, parent, &parent_ino) != 0) return -5;
    int found = ext2_find_entry_at(key, off, &m, parent_ino, name, &ino, &type);
    if (found != 1 || ino == 0) return -6;

    ext2_inode victim;
    if (ext2_read_inode_at(key, off, &m, ino, &victim) != 0) return -7;
    if ((victim.i_mode & 0170000) == 0040000 || type == 2) return -8;
    /* Guppy does not allocate triple-indirect blocks yet. Reject such a
       foreign inode before changing the parent directory. */
    if (victim.i_block[14]) return -19;

    uint8_t bb[4096], ib[4096], blk[4096], ptrblk[4096];
    if (m.block_size > sizeof bb) return -9;
    if (!pread_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) return -10;
    if (!pread_block(key, off, m.block_size, m.ib_blk, ib, m.block_size)) return -11;

    /* Remove the directory entry first. If it is not the first record in the
       block, merge its space into the previous record; otherwise mark it free. */
    ext2_inode pdir;
    if (ext2_read_inode_at(key, off, &m, parent_ino, &pdir) != 0) return -12;
    bool removed = false;
    size_t want = strlen(name);
    for (int bi = 0; bi < 12 && pdir.i_block[bi] && !removed; ++bi) {
        if (!pread_block(key, off, m.block_size, pdir.i_block[bi], blk, m.block_size))
            return -13;
        uint32_t pos = 0, prev = UINT32_MAX;
        while (pos + 8u <= m.block_size) {
            ext2_dirent *de = (ext2_dirent*)(blk + pos);
            if (de->rec_len < 8u || pos + de->rec_len > m.block_size) break;
            if (de->inode && de->name_len == want &&
                memcmp(de->name, name, want) == 0) {
                if (prev != UINT32_MAX) {
                    ext2_dirent *pde = (ext2_dirent*)(blk + prev);
                    pde->rec_len = (uint16_t)(pde->rec_len + de->rec_len);
                } else {
                    de->inode = 0;
                }
                if (!pwrite_block(key, off, m.block_size, pdir.i_block[bi],
                                  blk, m.block_size))
                    return -14;
                removed = true;
                break;
            }
            prev = pos;
            pos += de->rec_len;
        }
    }
    if (!removed) return -15;

    const uint32_t ptrs = m.block_size / 4u;
    uint32_t freed = 0;
#define FREE_BLOCK(b_) do { \
        uint32_t _b = (b_); \
        if (_b && _b < m.sb.s_blocks_count && \
            (bb[_b >> 3] & (uint8_t)(1u << (_b & 7u)))) { \
            bb[_b >> 3] &= (uint8_t)~(1u << (_b & 7u)); \
            ++freed; \
        } \
    } while (0)

    for (int i = 0; i < 12; ++i) FREE_BLOCK(victim.i_block[i]);

    if (victim.i_block[12]) {
        if (!pread_block(key, off, m.block_size, victim.i_block[12],
                         ptrblk, m.block_size)) return -16;
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t b = ((uint32_t*)ptrblk)[i];
            FREE_BLOCK(b);
        }
        FREE_BLOCK(victim.i_block[12]);
    }

    if (victim.i_block[13]) {
        if (!pread_block(key, off, m.block_size, victim.i_block[13],
                         ptrblk, m.block_size)) return -17;
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t leaf = ((uint32_t*)ptrblk)[i];
            if (!leaf) continue;
            uint8_t leafblk[4096];
            if (!pread_block(key, off, m.block_size, leaf, leafblk, m.block_size))
                return -18;
            for (uint32_t j = 0; j < ptrs; ++j) {
                uint32_t b = ((uint32_t*)leafblk)[j];
                FREE_BLOCK(b);
            }
            FREE_BLOCK(leaf);
        }
        FREE_BLOCK(victim.i_block[13]);
    }

#undef FREE_BLOCK

    uint32_t iidx = ino - 1u;
    ib[iidx >> 3] &= (uint8_t)~(1u << (iidx & 7u));

    memset(&victim, 0, sizeof victim);
    victim.i_dtime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, &m, ino, &victim) != 0) return -20;

    pdir.i_mtime = pdir.i_ctime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, &m, parent_ino, &pdir) != 0) return -21;

    m.sb.s_free_blocks_count += freed;
    m.gd.bg_free_blocks_count =
        (uint16_t)(m.gd.bg_free_blocks_count + freed);
    m.sb.s_free_inodes_count++;
    m.gd.bg_free_inodes_count++;

    return ext2_store_alloc_state(key, off, &m, bb, ib);
}


/* Truncate an existing regular file to zero bytes while keeping its inode and
   directory entry.  This is the EXT2 operation needed for O_TRUNC semantics. */
int ext2_truncate_at(const char *key, uint64_t off, const char *path, uint64_t size)
{
    if (!key || !path || size != 0) return -1;

    ext2_meta m;
    if (ext2_load_meta(key, off, &m) != 0) return -2;

    uint32_t ino = 0;
    if (ext2_resolve_path_at(key, off, &m, path, &ino) != 0 || ino == 0) return -3;

    ext2_inode file;
    if (ext2_read_inode_at(key, off, &m, ino, &file) != 0) return -4;
    if ((file.i_mode & 0170000) == 0040000) return -5;
    if (file.i_block[14]) return -6; /* triple-indirect unsupported */

    uint8_t bb[4096], ptrblk[4096];
    if (m.block_size > sizeof bb) return -7;
    if (!pread_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) return -8;

    const uint32_t ptrs = m.block_size / 4u;
    uint32_t freed = 0;

#define FREE_TRUNC_BLOCK(b_) do { \
        uint32_t _b = (b_); \
        if (_b && _b < m.sb.s_blocks_count && \
            (bb[_b >> 3] & (uint8_t)(1u << (_b & 7u)))) { \
            bb[_b >> 3] &= (uint8_t)~(1u << (_b & 7u)); \
            ++freed; \
        } \
    } while (0)

    for (int i = 0; i < 12; ++i) FREE_TRUNC_BLOCK(file.i_block[i]);

    if (file.i_block[12]) {
        if (!pread_block(key, off, m.block_size, file.i_block[12], ptrblk, m.block_size))
            return -9;
        for (uint32_t i = 0; i < ptrs; ++i)
            FREE_TRUNC_BLOCK(((uint32_t*)ptrblk)[i]);
        FREE_TRUNC_BLOCK(file.i_block[12]);
    }

    if (file.i_block[13]) {
        if (!pread_block(key, off, m.block_size, file.i_block[13], ptrblk, m.block_size))
            return -10;
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t leaf = ((uint32_t*)ptrblk)[i];
            if (!leaf) continue;
            uint8_t leafblk[4096];
            if (!pread_block(key, off, m.block_size, leaf, leafblk, m.block_size))
                return -11;
            for (uint32_t j = 0; j < ptrs; ++j)
                FREE_TRUNC_BLOCK(((uint32_t*)leafblk)[j]);
            FREE_TRUNC_BLOCK(leaf);
        }
        FREE_TRUNC_BLOCK(file.i_block[13]);
    }

#undef FREE_TRUNC_BLOCK

    memset(file.i_block, 0, sizeof file.i_block);
    file.i_size = 0;
    file.i_blocks = 0;
    file.i_mtime = file.i_ctime = (uint32_t)time(NULL);

    if (ext2_write_inode_at(key, off, &m, ino, &file) != 0) return -12;

    m.sb.s_free_blocks_count += freed;
    m.gd.bg_free_blocks_count = (uint16_t)(m.gd.bg_free_blocks_count + freed);

    if (!pwrite_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) return -13;
    if (!pwrite_bytes_at(key, off + 1024, &m.sb, sizeof m.sb)) return -14;
    if (!pwrite_block(key, off, m.block_size, m.gdt_blk, &m.gd, sizeof m.gd)) return -15;
    return 0;
}

/* Write new data into an existing regular-file inode.  The pathname and inode
   are preserved; only file data/size/timestamps change.  If the inode still
   owns blocks, truncate it first so this routine is safe when called directly. */
int ext2_write_existing_at(const char *key, uint64_t off,
                           const char *path, const void *data, size_t len)
{
    if (!key || !path || (!data && len)) return -1;
    if (len > UINT32_MAX) return -2;

    ext2_meta m;
    if (ext2_load_meta(key, off, &m) != 0) return -3;

    uint32_t ino = 0;
    if (ext2_resolve_path_at(key, off, &m, path, &ino) != 0 || ino == 0) return -4;

    ext2_inode file;
    if (ext2_read_inode_at(key, off, &m, ino, &file) != 0) return -5;
    if ((file.i_mode & 0170000) == 0040000) return -6;
    if (file.i_block[14]) return -7;

    bool owns_blocks = file.i_blocks != 0;
    if (!owns_blocks) {
        for (int i = 0; i < 15; ++i) {
            if (file.i_block[i]) { owns_blocks = true; break; }
        }
    }
    if (owns_blocks || file.i_size != 0) {
        if (ext2_truncate_at(key, off, path, 0) != 0) return -8;
        if (ext2_load_meta(key, off, &m) != 0) return -9;
        if (ext2_read_inode_at(key, off, &m, ino, &file) != 0) return -10;
    }

    const uint32_t ptrs = m.block_size / sizeof(uint32_t);
    const uint64_t max_data_blocks = 12ull + ptrs + (uint64_t)ptrs * ptrs;
    const uint64_t need_data = len ? ((uint64_t)len + m.block_size - 1u) / m.block_size : 0;
    if (need_data > max_data_blocks) return -11;

    uint8_t bb[4096];
    if (m.block_size > sizeof bb) return -12;
    if (!pread_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) return -13;

    uint32_t *single = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    uint32_t *droot  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    uint32_t *dleaf  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    if (!single || !droot || !dleaf) {
        free(single); free(droot); free(dleaf);
        return -14;
    }

    memset(file.i_block, 0, sizeof file.i_block);
    file.i_size = (uint32_t)len;
    file.i_atime = file.i_ctime = file.i_mtime = (uint32_t)time(NULL);

    uint32_t dleaf_block = 0;
    uint32_t dleaf_index = UINT32_MAX;
    uint32_t allocated_blocks = 0;
    size_t copied = 0;
    int rc = 0;
    uint8_t blockbuf[4096];

#define ALLOC_EXISTING_BLOCK(dst_) do { \
        (dst_) = find_free_block(&m, bb); \
        if (!(dst_)) { rc = -15; goto fail_existing; } \
        bb[(dst_) >> 3] |= (uint8_t)(1u << ((dst_) & 7u)); \
        allocated_blocks++; \
    } while (0)

    for (uint64_t lbn = 0; lbn < need_data; ++lbn) {
        uint32_t data_block = 0;
        ALLOC_EXISTING_BLOCK(data_block);

        memset(blockbuf, 0, m.block_size);
        size_t chunk = len - copied;
        if (chunk > m.block_size) chunk = m.block_size;
        if (chunk) memcpy(blockbuf, (const uint8_t*)data + copied, chunk);
        if (!pwrite_block(key, off, m.block_size, data_block, blockbuf, m.block_size)) {
            rc = -16; goto fail_existing;
        }
        copied += chunk;

        if (lbn < 12u) {
            file.i_block[lbn] = data_block;
        } else if (lbn < 12ull + ptrs) {
            if (file.i_block[12] == 0) ALLOC_EXISTING_BLOCK(file.i_block[12]);
            single[(uint32_t)(lbn - 12u)] = data_block;
        } else {
            uint64_t rel = lbn - 12ull - ptrs;
            uint32_t outer = (uint32_t)(rel / ptrs);
            uint32_t inner = (uint32_t)(rel % ptrs);

            if (file.i_block[13] == 0) ALLOC_EXISTING_BLOCK(file.i_block[13]);

            if (dleaf_index != outer) {
                if (dleaf_index != UINT32_MAX) {
                    if (!pwrite_block(key, off, m.block_size, dleaf_block,
                                      dleaf, m.block_size)) {
                        rc = -17; goto fail_existing;
                    }
                }
                memset(dleaf, 0, m.block_size);
                dleaf_block = 0;
                dleaf_index = outer;
                ALLOC_EXISTING_BLOCK(dleaf_block);
                droot[outer] = dleaf_block;
            }
            dleaf[inner] = data_block;
        }
    }

    if (file.i_block[12] &&
        !pwrite_block(key, off, m.block_size, file.i_block[12], single, m.block_size)) {
        rc = -18; goto fail_existing;
    }
    if (dleaf_index != UINT32_MAX &&
        !pwrite_block(key, off, m.block_size, dleaf_block, dleaf, m.block_size)) {
        rc = -19; goto fail_existing;
    }
    if (file.i_block[13] &&
        !pwrite_block(key, off, m.block_size, file.i_block[13], droot, m.block_size)) {
        rc = -20; goto fail_existing;
    }

    file.i_blocks = allocated_blocks * (m.block_size / 512u);
    if (ext2_write_inode_at(key, off, &m, ino, &file) != 0) {
        rc = -21; goto fail_existing;
    }

    if (m.sb.s_free_blocks_count >= allocated_blocks)
        m.sb.s_free_blocks_count -= allocated_blocks;
    else
        m.sb.s_free_blocks_count = 0;
    if (m.gd.bg_free_blocks_count >= allocated_blocks)
        m.gd.bg_free_blocks_count -= (uint16_t)allocated_blocks;
    else
        m.gd.bg_free_blocks_count = 0;

    if (!pwrite_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) {
        rc = -22; goto fail_existing;
    }
    if (!pwrite_bytes_at(key, off + 1024, &m.sb, sizeof m.sb)) {
        rc = -23; goto fail_existing;
    }
    if (!pwrite_block(key, off, m.block_size, m.gdt_blk, &m.gd, sizeof m.gd)) {
        rc = -24; goto fail_existing;
    }

fail_existing:
    free(single);
    free(droot);
    free(dleaf);
    return rc;

#undef ALLOC_EXISTING_BLOCK
}

int ext2_create_and_write(const char *key, uint64_t off,
                          const char *path, const void *data, size_t len)
{
    if (!key || !path || (!data && len)) return -1;

    ext2_meta m;
    if (ext2_load_meta(key, off, &m) != 0) return -2;

    char parent[512], name[256];
    if (split_parent_name(path, parent, sizeof parent, name, sizeof name) != 0) return -3;

    uint32_t parent_ino = 0;
    if (ext2_resolve_path_at(key, off, &m, parent, &parent_ino) != 0) return -4;
    if (ext2_find_entry_at(key, off, &m, parent_ino, name, NULL, NULL) == 1) return -5;

    if (len > UINT32_MAX) return -6; /* classic EXT2 i_size used by Guppy */

    const uint32_t ptrs = m.block_size / sizeof(uint32_t);
    const uint64_t max_data_blocks = 12ull + ptrs + (uint64_t)ptrs * ptrs;
    const uint64_t need_data =
        len ? ((uint64_t)len + m.block_size - 1u) / m.block_size : 0;

    if (need_data > max_data_blocks) return -7; /* triple indirect not yet needed */

    uint8_t bb[4096], ib[4096];
    if (m.block_size > sizeof bb) return -8;
    if (!pread_block(key, off, m.block_size, m.bb_blk, bb, m.block_size)) return -9;
    if (!pread_block(key, off, m.block_size, m.ib_blk, ib, m.block_size)) return -10;

    uint32_t ino = find_free_inode(&m, ib);
    if (!ino) return -11;

    uint32_t iidx = ino - 1u;
    ib[iidx >> 3] |= (uint8_t)(1u << (iidx & 7u));

    ext2_inode file;
    memset(&file, 0, sizeof file);
    file.i_mode = 0100644;
    file.i_links_count = 1;
    file.i_size = (uint32_t)len;
    file.i_atime = file.i_ctime = file.i_mtime = (uint32_t)time(NULL);

    uint32_t *single = NULL;
    uint32_t *droot = NULL;
    uint32_t *dleaf = NULL;
    uint32_t dleaf_block = 0;
    uint32_t dleaf_index = UINT32_MAX;
    uint32_t allocated_blocks = 0;
    size_t copied = 0;

    single = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    droot  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    dleaf  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    if (!single || !droot || !dleaf) {
        free(single); free(droot); free(dleaf);
        return -12;
    }

#define ALLOC_BLOCK(dst_) do { \
        (dst_) = find_free_block(&m, bb); \
        if (!(dst_)) { rc = -13; goto fail; } \
        bb[(dst_) >> 3] |= (uint8_t)(1u << ((dst_) & 7u)); \
        allocated_blocks++; \
    } while (0)

    int rc = 0;
    uint8_t blockbuf[4096];

    for (uint64_t lbn = 0; lbn < need_data; ++lbn) {
        uint32_t data_block = 0;
        ALLOC_BLOCK(data_block);

        memset(blockbuf, 0, m.block_size);
        size_t chunk = len - copied;
        if (chunk > m.block_size) chunk = m.block_size;
        if (chunk) memcpy(blockbuf, (const uint8_t*)data + copied, chunk);
        if (!pwrite_block(key, off, m.block_size, data_block, blockbuf, m.block_size)) {
            rc = -14; goto fail;
        }
        copied += chunk;

        if (lbn < 12u) {
            file.i_block[lbn] = data_block;
        } else if (lbn < 12ull + ptrs) {
            if (file.i_block[12] == 0) {
                ALLOC_BLOCK(file.i_block[12]);
            }
            single[(uint32_t)(lbn - 12u)] = data_block;
        } else {
            uint64_t rel = lbn - 12ull - ptrs;
            uint32_t outer = (uint32_t)(rel / ptrs);
            uint32_t inner = (uint32_t)(rel % ptrs);

            if (file.i_block[13] == 0) {
                ALLOC_BLOCK(file.i_block[13]);
            }

            if (dleaf_index != outer) {
                /* Flush the previous second-level table before moving on. */
                if (dleaf_index != UINT32_MAX) {
                    if (!pwrite_block(key, off, m.block_size, dleaf_block,
                                      dleaf, m.block_size)) {
                        rc = -15; goto fail;
                    }
                }

                memset(dleaf, 0, m.block_size);
                dleaf_block = 0;
                dleaf_index = outer;

                ALLOC_BLOCK(dleaf_block);
                droot[outer] = dleaf_block;
            }

            dleaf[inner] = data_block;
        }
    }

    if (file.i_block[12] != 0 &&
        !pwrite_block(key, off, m.block_size, file.i_block[12],
                      single, m.block_size)) {
        rc = -16; goto fail;
    }

    if (dleaf_index != UINT32_MAX &&
        !pwrite_block(key, off, m.block_size, dleaf_block,
                      dleaf, m.block_size)) {
        rc = -17; goto fail;
    }

    if (file.i_block[13] != 0 &&
        !pwrite_block(key, off, m.block_size, file.i_block[13],
                      droot, m.block_size)) {
        rc = -18; goto fail;
    }

    file.i_blocks = allocated_blocks * (m.block_size / 512u);

    if (ext2_write_inode_at(key, off, &m, ino, &file) != 0) {
        rc = -19; goto fail;
    }

    {
        uint32_t extra = find_free_block(&m, bb);
        int arc = ext2_append_dirent_at(key, off, &m, parent_ino, ino, 1, name, &extra);
        if (arc < 0) { rc = -20; goto fail; }
        if (arc == 1) {
            bb[extra >> 3] |= (uint8_t)(1u << (extra & 7u));
            allocated_blocks++;
        }
    }

    if (m.sb.s_free_inodes_count) m.sb.s_free_inodes_count--;
    if (m.gd.bg_free_inodes_count) m.gd.bg_free_inodes_count--;
    if (m.sb.s_free_blocks_count >= allocated_blocks)
        m.sb.s_free_blocks_count -= allocated_blocks;
    else
        m.sb.s_free_blocks_count = 0;
    if (m.gd.bg_free_blocks_count >= allocated_blocks)
        m.gd.bg_free_blocks_count -= (uint16_t)allocated_blocks;
    else
        m.gd.bg_free_blocks_count = 0;

    rc = ext2_store_alloc_state(key, off, &m, bb, ib);

fail:
    free(single);
    free(droot);
    free(dleaf);
    return rc;

#undef ALLOC_BLOCK
}

