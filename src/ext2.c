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
    const uint32_t block_size = 4096u;
    const uint32_t blocks_per_group = block_size * 8u;   /* one bitmap block */
    const uint32_t inode_size = 128u;
    const uint32_t inodes_per_group = 1024u;
    const uint32_t inode_tbl_blocks =
        (inodes_per_group * inode_size + block_size - 1u) / block_size;
    const uint32_t reserved_inodes = 10u;

    if (bytes < 64 * 1024) {
        fprintf(stderr, "mkfs.ext2: device too small (%" PRIu64 " bytes)\n", bytes);
        return -1;
    }
    if (bytes / block_size > UINT32_MAX) {
        fprintf(stderr, "mkfs.ext2: filesystem too large for classic 32-bit EXT2 blocks\n");
        return -1;
    }

    const uint32_t total_blocks = (uint32_t)(bytes / block_size);
    const uint32_t groups = (total_blocks + blocks_per_group - 1u) / blocks_per_group;
    const uint32_t gd_per_block = block_size / (uint32_t)sizeof(ext2_group_desc);
    const uint32_t gdt_blocks = (groups + gd_per_block - 1u) / gd_per_block;
    if (groups == 0 || groups > 65535u) {
        fprintf(stderr, "mkfs.ext2: unsupported block-group count %u\n", groups);
        return -1;
    }

    ext2_group_desc *gdt = (ext2_group_desc*)calloc(groups, sizeof(*gdt));
    if (!gdt) return -1;

    ext2_superblock sb; memset(&sb, 0, sizeof sb);
    sb.s_inodes_count      = groups * inodes_per_group;
    sb.s_blocks_count      = total_blocks;
    sb.s_r_blocks_count    = 0;
    sb.s_first_data_block  = 0;
    sb.s_log_block_size    = 2;
    sb.s_log_frag_size     = 2;
    sb.s_blocks_per_group  = blocks_per_group;
    sb.s_frags_per_group   = blocks_per_group;
    sb.s_inodes_per_group  = inodes_per_group;
    sb.s_mtime = sb.s_wtime = (uint32_t)time(NULL);
    sb.s_max_mnt_count     = 20;
    sb.s_magic             = 0xEF53;
    sb.s_state             = 1;
    sb.s_errors            = 1;
    sb.s_lastcheck         = sb.s_mtime;
    sb.s_creator_os        = 0;
    sb.s_rev_level         = 1;
    sb.s_first_ino         = 11;
    sb.s_inode_size        = inode_size;
    sb.s_feature_compat    = 0;
    sb.s_feature_incompat  = 0;
    sb.s_feature_ro_compat = 0; /* full backup super/GDT in every group */
    if (label) snprintf(sb.s_volume_name, sizeof sb.s_volume_name, "%s", label);

    uint64_t total_free_blocks = 0;
    uint64_t total_free_inodes = 0;
    uint8_t zero[4096]; memset(zero, 0, sizeof zero);

    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t group_start = g * blocks_per_group;
        const uint32_t group_blocks =
            (total_blocks - group_start > blocks_per_group) ? blocks_per_group : (total_blocks - group_start);
        const uint32_t super_rel = 0u;
        const uint32_t gdt_rel = 1u;
        const uint32_t bb_rel = gdt_rel + gdt_blocks;
        const uint32_t ib_rel = bb_rel + 1u;
        const uint32_t it_rel = ib_rel + 1u;
        const uint32_t data_rel = it_rel + inode_tbl_blocks;
        if (group_blocks <= data_rel) {
            free(gdt);
            fprintf(stderr, "mkfs.ext2: final block group too small for metadata\n");
            return -1;
        }

        gdt[g].bg_block_bitmap = group_start + bb_rel;
        gdt[g].bg_inode_bitmap = group_start + ib_rel;
        gdt[g].bg_inode_table  = group_start + it_rel;

        uint8_t bb[4096]; memset(bb, 0, sizeof bb);
        uint8_t ib[4096]; memset(ib, 0, sizeof ib);

        /* Every group contains a backup superblock and GDT, then its bitmaps/table. */
        for (uint32_t r = super_rel; r < data_rel; ++r) set_bit(bb, r);
        /* Bits beyond the physical end of a short final group are unavailable. */
        for (uint32_t r = group_blocks; r < blocks_per_group; ++r) set_bit(bb, r);

        uint32_t used_data = 0;
        if (g == 0) {
            /* Root directory consumes the first data block in group 0. */
            set_bit(bb, data_rel);
            used_data = 1;
            for (uint32_t ino = 1; ino <= reserved_inodes; ++ino) set_bit(ib, ino - 1u);
        }

        const uint32_t metadata_used = data_rel;
        const uint32_t free_blocks = group_blocks - metadata_used - used_data;
        const uint32_t free_inodes = inodes_per_group - (g == 0 ? reserved_inodes : 0u);
        gdt[g].bg_free_blocks_count = (uint16_t)free_blocks;
        gdt[g].bg_free_inodes_count = (uint16_t)free_inodes;
        gdt[g].bg_used_dirs_count = (g == 0) ? 1u : 0u;
        total_free_blocks += free_blocks;
        total_free_inodes += free_inodes;

        /* Clear metadata area, inode table, and root headroom. */
        for (uint32_t r = 0; r < data_rel + (g == 0 ? 1u : 0u); ++r) {
            if (!pwrite_block(key, off, block_size, group_start + r, zero, sizeof zero)) {
                free(gdt); return -1;
            }
        }
        if (!pwrite_block(key, off, block_size, gdt[g].bg_block_bitmap, bb, sizeof bb) ||
            !pwrite_block(key, off, block_size, gdt[g].bg_inode_bitmap, ib, sizeof ib)) {
            free(gdt); return -1;
        }
    }

    sb.s_free_blocks_count = (uint32_t)total_free_blocks;
    sb.s_free_inodes_count = (uint32_t)total_free_inodes;

    /* Primary superblock is always at filesystem byte offset 1024. */
    if (!pwrite_bytes_at(key, off + 1024u, &sb, sizeof sb)) { free(gdt); return -1; }

    /* Write primary and backup superblock/GDT copies. */
    for (uint32_t g = 0; g < groups; ++g) {
        const uint32_t group_start = g * blocks_per_group;
        if (g != 0) {
            ext2_superblock backup = sb;
            backup.s_block_group_nr = (uint16_t)g;
            if (!pwrite_block(key, off, block_size, group_start, &backup, sizeof backup)) {
                free(gdt); return -1;
            }
        }
        for (uint32_t k = 0; k < gdt_blocks; ++k) {
            uint8_t gbuf[4096]; memset(gbuf, 0, sizeof gbuf);
            uint32_t first = k * gd_per_block;
            uint32_t count = groups - first;
            if (count > gd_per_block) count = gd_per_block;
            memcpy(gbuf, gdt + first, count * sizeof(ext2_group_desc));
            if (!pwrite_block(key, off, block_size, group_start + 1u + k, gbuf, sizeof gbuf)) {
                free(gdt); return -1;
            }
        }
    }

    /* Root inode (#2) lives in group 0. */
    const uint32_t root_data_block = gdt[0].bg_inode_table + inode_tbl_blocks;
    ext2_inode root; memset(&root, 0, sizeof root);
    root.i_mode = 040755;
    root.i_links_count = 2;
    root.i_size = block_size;
    root.i_atime = root.i_ctime = root.i_mtime = (uint32_t)time(NULL);
    root.i_blocks = block_size / 512u;
    root.i_block[0] = root_data_block;

    uint8_t itbuf[4096]; memset(itbuf, 0, sizeof itbuf);
    const uint32_t inodes_per_block = block_size / inode_size;
    const uint32_t root_index = 1u;
    const uint32_t root_tbl_rel = root_index / inodes_per_block;
    const uint32_t root_tbl_off = (root_index % inodes_per_block) * inode_size;
    memcpy(itbuf + root_tbl_off, &root, sizeof root);
    if (!pwrite_block(key, off, block_size, gdt[0].bg_inode_table + root_tbl_rel, itbuf, sizeof itbuf)) {
        free(gdt); return -1;
    }

    uint8_t dirblk[4096]; memset(dirblk, 0, sizeof dirblk);
    ext2_dirent *dot = (ext2_dirent*)dirblk;
    dot->inode = 2; dot->rec_len = 12; dot->name_len = 1; dot->file_type = 2; dot->name[0] = '.';
    ext2_dirent *dotdot = (ext2_dirent*)(dirblk + 12);
    dotdot->inode = 2; dotdot->rec_len = (uint16_t)(block_size - 12u);
    dotdot->name_len = 2; dotdot->file_type = 2; dotdot->name[0] = '.'; dotdot->name[1] = '.';
    if (!pwrite_block(key, off, block_size, root_data_block, dirblk, sizeof dirblk)) {
        free(gdt); return -1;
    }

    free(gdt);
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

#define EXT2_MAX_GROUPS 1024u

typedef struct {
    ext2_superblock sb;
    ext2_group_desc gd[EXT2_MAX_GROUPS];
    uint32_t group_count;
    uint32_t block_size;
    uint32_t sb_blk;
    uint32_t gdt_blk;
    uint32_t gdt_blocks;
    uint32_t inode_tbl_blocks;
} ext2_meta;

static uint32_t ext2_group_block_count(const ext2_meta *m, uint32_t g) {
    uint64_t first = (uint64_t)g * m->sb.s_blocks_per_group;
    if (first >= m->sb.s_blocks_count) return 0;
    uint64_t left = (uint64_t)m->sb.s_blocks_count - first;
    return (uint32_t)(left > m->sb.s_blocks_per_group ? m->sb.s_blocks_per_group : left);
}

static int ext2_store_meta(const char *key, uint64_t off, ext2_meta *m) {
    if (!pwrite_bytes_at(key, off + 1024u, &m->sb, sizeof m->sb)) return -1;
    const uint32_t per = m->block_size / (uint32_t)sizeof(ext2_group_desc);
    for (uint32_t k = 0; k < m->gdt_blocks; ++k) {
        uint8_t buf[4096]; memset(buf, 0, m->block_size);
        uint32_t first = k * per;
        uint32_t count = m->group_count - first;
        if (count > per) count = per;
        memcpy(buf, &m->gd[first], count * sizeof(ext2_group_desc));
        if (!pwrite_block(key, off, m->block_size, m->gdt_blk + k, buf, m->block_size)) return -2;
    }
    return 0;
}

static int ext2_load_meta(const char *key, uint64_t off, ext2_meta *m) {
    if (!key || !m) return -1;
    memset(m, 0, sizeof *m);
    if (!pread_bytes_at(key, off + 1024, &m->sb, sizeof m->sb)) return -2;
    if (m->sb.s_magic != 0xEF53) return -3;
    m->block_size = 1024u << m->sb.s_log_block_size;
    if (m->block_size == 0 || m->block_size > 4096u) return -4;
    if (!m->sb.s_blocks_per_group || !m->sb.s_inodes_per_group) return -5;
    if (m->sb.s_inode_size == 0) m->sb.s_inode_size = 128;
    m->group_count = (m->sb.s_blocks_count + m->sb.s_blocks_per_group - 1u) / m->sb.s_blocks_per_group;
    if (!m->group_count || m->group_count > EXT2_MAX_GROUPS) return -6;
    m->sb_blk = (m->block_size == 1024u) ? 1u : 0u;
    m->gdt_blk = m->sb_blk + 1u;
    const uint32_t per = m->block_size / (uint32_t)sizeof(ext2_group_desc);
    m->gdt_blocks = (m->group_count + per - 1u) / per;
    m->inode_tbl_blocks =
        (m->sb.s_inodes_per_group * m->sb.s_inode_size + m->block_size - 1u) / m->block_size;
    for (uint32_t k = 0; k < m->gdt_blocks; ++k) {
        uint8_t buf[4096];
        if (!pread_block(key, off, m->block_size, m->gdt_blk + k, buf, m->block_size)) return -7;
        uint32_t first = k * per;
        uint32_t count = m->group_count - first;
        if (count > per) count = per;
        memcpy(&m->gd[first], buf, count * sizeof(ext2_group_desc));
    }
    return 0;
}

static int ext2_read_inode_at(const char *key, uint64_t off, const ext2_meta *m,
                              uint32_t ino, ext2_inode *out) {
    if (!key || !m || !out || ino == 0 || ino > m->sb.s_inodes_count) return -1;
    const uint32_t group = (ino - 1u) / m->sb.s_inodes_per_group;
    const uint32_t local = (ino - 1u) % m->sb.s_inodes_per_group;
    if (group >= m->group_count) return -2;
    const uint32_t inodes_per_block = m->block_size / m->sb.s_inode_size;
    if (inodes_per_block == 0) return -3;
    const uint32_t tbl_rel_blk = local / inodes_per_block;
    const uint32_t tbl_off = (local % inodes_per_block) * m->sb.s_inode_size;
    uint8_t buf[4096];
    if (!pread_block(key, off, m->block_size, m->gd[group].bg_inode_table + tbl_rel_blk,
                     buf, m->block_size)) return -4;
    memset(out, 0, sizeof *out);
    memcpy(out, buf + tbl_off, sizeof *out);
    return 0;
}

static int ext2_write_inode_at(const char *key, uint64_t off, const ext2_meta *m,
                               uint32_t ino, const ext2_inode *in) {
    if (!key || !m || !in || ino == 0 || ino > m->sb.s_inodes_count) return -1;
    const uint32_t group = (ino - 1u) / m->sb.s_inodes_per_group;
    const uint32_t local = (ino - 1u) % m->sb.s_inodes_per_group;
    if (group >= m->group_count) return -2;
    const uint32_t inodes_per_block = m->block_size / m->sb.s_inode_size;
    if (inodes_per_block == 0) return -3;
    const uint32_t tbl_rel_blk = local / inodes_per_block;
    const uint32_t tbl_off = (local % inodes_per_block) * m->sb.s_inode_size;
    uint8_t buf[4096];
    if (!pread_block(key, off, m->block_size, m->gd[group].bg_inode_table + tbl_rel_blk,
                     buf, m->block_size)) return -4;
    memcpy(buf + tbl_off, in, sizeof *in);
    if (!pwrite_block(key, off, m->block_size, m->gd[group].bg_inode_table + tbl_rel_blk,
                      buf, m->block_size)) return -5;
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


static uint32_t ext2_alloc_inode(const char *key, uint64_t off, ext2_meta *m) {
    uint8_t ib[4096];
    for (uint32_t g = 0; g < m->group_count; ++g) {
        if (!m->gd[g].bg_free_inodes_count) continue;
        if (!pread_block(key, off, m->block_size, m->gd[g].bg_inode_bitmap, ib, m->block_size)) return 0;
        for (uint32_t local = 0; local < m->sb.s_inodes_per_group; ++local) {
            uint32_t ino = g * m->sb.s_inodes_per_group + local + 1u;
            if (ino > m->sb.s_inodes_count) break;
            if (ino < m->sb.s_first_ino && ino != 2u) continue;
            if (!(ib[local >> 3] & (uint8_t)(1u << (local & 7u)))) {
                ib[local >> 3] |= (uint8_t)(1u << (local & 7u));
                if (!pwrite_block(key, off, m->block_size, m->gd[g].bg_inode_bitmap, ib, m->block_size)) return 0;
                if (m->gd[g].bg_free_inodes_count) m->gd[g].bg_free_inodes_count--;
                if (m->sb.s_free_inodes_count) m->sb.s_free_inodes_count--;
                if (ext2_store_meta(key, off, m) != 0) return 0;
                return ino;
            }
        }
    }
    return 0;
}

static int ext2_free_inode_num(const char *key, uint64_t off, ext2_meta *m, uint32_t ino) {
    if (!ino || ino > m->sb.s_inodes_count) return -1;
    uint32_t g = (ino - 1u) / m->sb.s_inodes_per_group;
    uint32_t local = (ino - 1u) % m->sb.s_inodes_per_group;
    uint8_t ib[4096];
    if (!pread_block(key, off, m->block_size, m->gd[g].bg_inode_bitmap, ib, m->block_size)) return -2;
    uint8_t mask = (uint8_t)(1u << (local & 7u));
    if (ib[local >> 3] & mask) {
        ib[local >> 3] &= (uint8_t)~mask;
        if (!pwrite_block(key, off, m->block_size, m->gd[g].bg_inode_bitmap, ib, m->block_size)) return -3;
        m->gd[g].bg_free_inodes_count++;
        m->sb.s_free_inodes_count++;
        if (ext2_store_meta(key, off, m) != 0) return -4;
    }
    return 0;
}

static uint32_t ext2_alloc_block(const char *key, uint64_t off, ext2_meta *m) {
    uint8_t bb[4096];
    for (uint32_t g = 0; g < m->group_count; ++g) {
        if (!m->gd[g].bg_free_blocks_count) continue;
        if (!pread_block(key, off, m->block_size, m->gd[g].bg_block_bitmap, bb, m->block_size)) return 0;
        uint32_t group_blocks = ext2_group_block_count(m, g);
        for (uint32_t local = 0; local < group_blocks; ++local) {
            if (!(bb[local >> 3] & (uint8_t)(1u << (local & 7u)))) {
                bb[local >> 3] |= (uint8_t)(1u << (local & 7u));
                if (!pwrite_block(key, off, m->block_size, m->gd[g].bg_block_bitmap, bb, m->block_size)) return 0;
                if (m->gd[g].bg_free_blocks_count) m->gd[g].bg_free_blocks_count--;
                if (m->sb.s_free_blocks_count) m->sb.s_free_blocks_count--;
                if (ext2_store_meta(key, off, m) != 0) return 0;
                return g * m->sb.s_blocks_per_group + local;
            }
        }
    }
    return 0;
}


/* Sequential-allocation context used by large regular-file writes.
   The original allocator performed a bitmap read + full scan + bitmap write +
   superblock/GDT write for every 4 KiB block.  Besides excessive I/O, rescanning
   from bit zero made allocation effectively quadratic as a file grew.

   A write transaction keeps each touched group's bitmap in memory, remembers a
   forward allocation cursor, updates free-space counters in ext2_meta, and
   persists dirty bitmaps plus the superblock/GDT once at commit. */
typedef struct {
    uint8_t  *bitmaps;      /* group_count * block_size bytes */
    uint32_t *cursor;       /* next bit to try in each group */
    uint8_t  *loaded;
    uint8_t  *dirty;
} ext2_alloc_ctx;

static int ext2_alloc_ctx_init(const ext2_meta *m, ext2_alloc_ctx *ctx) {
    if (!m || !ctx || !m->group_count || !m->block_size) return -1;
    memset(ctx, 0, sizeof *ctx);
    if ((uint64_t)m->group_count * m->block_size > SIZE_MAX) return -2;
    ctx->bitmaps = (uint8_t*)calloc(m->group_count, m->block_size);
    ctx->cursor  = (uint32_t*)calloc(m->group_count, sizeof(uint32_t));
    ctx->loaded  = (uint8_t*)calloc(m->group_count, 1);
    ctx->dirty   = (uint8_t*)calloc(m->group_count, 1);
    if (!ctx->bitmaps || !ctx->cursor || !ctx->loaded || !ctx->dirty) {
        free(ctx->bitmaps); free(ctx->cursor); free(ctx->loaded); free(ctx->dirty);
        memset(ctx, 0, sizeof *ctx);
        return -3;
    }
    return 0;
}

static void ext2_alloc_ctx_destroy(ext2_alloc_ctx *ctx) {
    if (!ctx) return;
    free(ctx->bitmaps); free(ctx->cursor); free(ctx->loaded); free(ctx->dirty);
    memset(ctx, 0, sizeof *ctx);
}

static uint32_t ext2_alloc_block_ctx(const char *key, uint64_t off,
                                     ext2_meta *m, ext2_alloc_ctx *ctx) {
    if (!key || !m || !ctx) return 0;
    for (uint32_t g = 0; g < m->group_count; ++g) {
        if (!m->gd[g].bg_free_blocks_count) continue;

        uint8_t *bb = ctx->bitmaps + (size_t)g * m->block_size;
        if (!ctx->loaded[g]) {
            if (!pread_block(key, off, m->block_size,
                             m->gd[g].bg_block_bitmap, bb, m->block_size)) return 0;
            ctx->loaded[g] = 1;
        }

        const uint32_t group_blocks = ext2_group_block_count(m, g);
        uint32_t local = ctx->cursor[g];
        if (local > group_blocks) local = group_blocks;
        for (; local < group_blocks; ++local) {
            const uint8_t mask = (uint8_t)(1u << (local & 7u));
            if (!(bb[local >> 3] & mask)) {
                bb[local >> 3] |= mask;
                ctx->cursor[g] = local + 1u;
                ctx->dirty[g] = 1;
                if (m->gd[g].bg_free_blocks_count) m->gd[g].bg_free_blocks_count--;
                if (m->sb.s_free_blocks_count) m->sb.s_free_blocks_count--;
                return g * m->sb.s_blocks_per_group + local;
            }
        }
        ctx->cursor[g] = group_blocks;
    }
    return 0;
}

static int ext2_alloc_ctx_commit(const char *key, uint64_t off,
                                 ext2_meta *m, ext2_alloc_ctx *ctx) {
    if (!key || !m || !ctx) return -1;
    for (uint32_t g = 0; g < m->group_count; ++g) {
        if (!ctx->dirty[g]) continue;
        uint8_t *bb = ctx->bitmaps + (size_t)g * m->block_size;
        if (!pwrite_block(key, off, m->block_size,
                          m->gd[g].bg_block_bitmap, bb, m->block_size)) return -2;
    }
    return ext2_store_meta(key, off, m);
}

/* Write a regular file's data using maximal physically-contiguous runs.
   The allocator normally hands out sequential blocks; indirect metadata blocks
   create only occasional gaps.  Coalescing the data blocks turns thousands of
   fopen/seek/write/close cycles in diskio into a small number of large writes. */
static int ext2_write_data_runs(const char *key, uint64_t off, const ext2_meta *m,
                                const void *data, size_t len,
                                const uint32_t *blocks, uint64_t count) {
    if ((!data && len) || (!blocks && count)) return -1;
    if (!count) return 0;
    const uint8_t *src = (const uint8_t*)data;
    uint64_t i = 0;
    while (i < count) {
        uint64_t j = i + 1u;
        while (j < count && blocks[j] == blocks[j - 1u] + 1u) ++j;

        const uint64_t file_pos = i * (uint64_t)m->block_size;
        const uint64_t capacity = (j - i) * (uint64_t)m->block_size;
        uint64_t actual = (file_pos < len) ? (uint64_t)len - file_pos : 0;
        if (actual > capacity) actual = capacity;
        uint64_t abs = off + (uint64_t)blocks[i] * m->block_size;
        uint64_t done = 0;
        while (done < actual) {
            uint64_t left = actual - done;
            uint32_t chunk = (left > (64u * 1024u * 1024u))
                           ? (64u * 1024u * 1024u) : (uint32_t)left;
            if (!pwrite_bytes_at(key, abs + done, src + file_pos + done, chunk)) return -2;
            done += chunk;
        }
        if (actual < capacity) {
            uint8_t zero[4096]; memset(zero, 0, sizeof zero);
            uint64_t tail = capacity - actual;
            while (tail) {
                uint32_t chunk = tail > sizeof zero ? (uint32_t)sizeof zero : (uint32_t)tail;
                if (!pwrite_bytes_at(key, abs + actual, zero, chunk)) return -3;
                actual += chunk;
                tail -= chunk;
            }
        }
        i = j;
    }
    return 0;
}

static int ext2_free_block_ctx(const char *key, uint64_t off,
                               ext2_meta *m, ext2_alloc_ctx *ctx, uint32_t block) {
    if (!block || block >= m->sb.s_blocks_count) return -1;
    const uint32_t g = block / m->sb.s_blocks_per_group;
    const uint32_t local = block % m->sb.s_blocks_per_group;
    if (g >= m->group_count) return -2;
    uint8_t *bb = ctx->bitmaps + (size_t)g * m->block_size;
    if (!ctx->loaded[g]) {
        if (!pread_block(key, off, m->block_size,
                         m->gd[g].bg_block_bitmap, bb, m->block_size)) return -3;
        ctx->loaded[g] = 1;
    }
    const uint8_t mask = (uint8_t)(1u << (local & 7u));
    if (bb[local >> 3] & mask) {
        bb[local >> 3] &= (uint8_t)~mask;
        ctx->dirty[g] = 1;
        m->gd[g].bg_free_blocks_count++;
        m->sb.s_free_blocks_count++;
    }
    return 0;
}

/* Insert a directory entry. If all existing direct blocks are full, allocate
   one additional directory block from the filesystem. */
static int ext2_append_dirent_at(const char *key, uint64_t off, ext2_meta *m,
                                 uint32_t dir_ino, uint32_t child_ino,
                                 uint8_t file_type, const char *name) {
    ext2_inode dir;
    if (ext2_read_inode_at(key, off, m, dir_ino, &dir) != 0) return -1;
    if ((dir.i_mode & 0170000) != 0040000) return -2;
    uint8_t nl = (uint8_t)strlen(name);
    uint16_t need = dirent_min_rec_len(nl);
    uint8_t blk[4096];

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
                ext2_dirent *ne = (ext2_dirent*)(blk + pos + min);
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
    if (slot < 0) return -6;
    uint32_t extra = ext2_alloc_block(key, off, m);
    if (!extra) return -7;
    memset(blk, 0, m->block_size);
    ext2_dirent *ne = (ext2_dirent*)blk;
    ne->inode = child_ino;
    ne->rec_len = (uint16_t)m->block_size;
    ne->name_len = nl;
    ne->file_type = file_type;
    memcpy(ne->name, name, nl);
    if (!pwrite_block(key, off, m->block_size, extra, blk, m->block_size)) return -8;
    dir.i_block[slot] = extra;
    dir.i_size += m->block_size;
    dir.i_blocks += m->block_size / 512u;
    dir.i_mtime = dir.i_ctime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, m, dir_ino, &dir) != 0) return -9;
    return 1;
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

    uint32_t ino = ext2_alloc_inode(key, off, &m);
    if (!ino) return -24;
    uint32_t data_blk = ext2_alloc_block(key, off, &m);
    if (!data_blk) return -25;

    ext2_inode child; memset(&child, 0, sizeof child);
    child.i_mode = (uint16_t)(0040000 | (mode ? (mode & 0777u) : 0755u));
    child.i_size = m.block_size;
    child.i_atime = child.i_ctime = child.i_mtime = (uint32_t)time(NULL);
    child.i_links_count = 2;
    child.i_blocks = m.block_size / 512u;
    child.i_block[0] = data_blk;
    if (ext2_write_inode_at(key, off, &m, ino, &child) != 0) return -26;

    uint8_t dirblk[4096]; memset(dirblk, 0, m.block_size);
    ext2_dirent *dot = (ext2_dirent*)dirblk;
    dot->inode = ino; dot->rec_len = 12; dot->name_len = 1; dot->file_type = 2; dot->name[0] = '.';
    ext2_dirent *dotdot = (ext2_dirent*)(dirblk + 12);
    dotdot->inode = parent_ino; dotdot->rec_len = (uint16_t)(m.block_size - 12u);
    dotdot->name_len = 2; dotdot->file_type = 2; dotdot->name[0] = '.'; dotdot->name[1] = '.';
    if (!pwrite_block(key, off, m.block_size, data_blk, dirblk, m.block_size)) return -27;

    if (ext2_append_dirent_at(key, off, &m, parent_ino, ino, 2, name) < 0) return -28;

    ext2_inode parent_inode;
    if (ext2_read_inode_at(key, off, &m, parent_ino, &parent_inode) != 0) return -29;
    parent_inode.i_links_count++;
    parent_inode.i_mtime = parent_inode.i_ctime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, &m, parent_ino, &parent_inode) != 0) return -30;

    uint32_t ig = (ino - 1u) / m.sb.s_inodes_per_group;
    m.gd[ig].bg_used_dirs_count++;
    return ext2_store_meta(key, off, &m);
}

static int ext2_free_inode_data_blocks(const char *key, uint64_t off,
                                       ext2_meta *m, const ext2_inode *file) {
    if (file->i_block[14]) return -1; /* future triple-indirect / large-file support */
    const uint32_t ptrs = m->block_size / 4u;
    uint8_t ptrblk[4096];
    ext2_alloc_ctx fctx;
    if (ext2_alloc_ctx_init(m, &fctx) != 0) return -2;
    int rc = 0;

#define FREE_CTX_BLOCK(block_, code_) do { \
        if ((block_) && ext2_free_block_ctx(key, off, m, &fctx, (block_)) != 0) { \
            rc = (code_); goto out; \
        } \
    } while (0)

    for (int i = 0; i < 12; ++i) FREE_CTX_BLOCK(file->i_block[i], -3);

    if (file->i_block[12]) {
        if (!pread_block(key, off, m->block_size, file->i_block[12], ptrblk, m->block_size)) {
            rc = -4; goto out;
        }
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t b = ((uint32_t*)ptrblk)[i];
            FREE_CTX_BLOCK(b, -5);
        }
        FREE_CTX_BLOCK(file->i_block[12], -6);
    }

    if (file->i_block[13]) {
        if (!pread_block(key, off, m->block_size, file->i_block[13], ptrblk, m->block_size)) {
            rc = -7; goto out;
        }
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t leaf = ((uint32_t*)ptrblk)[i];
            if (!leaf) continue;
            uint8_t leafblk[4096];
            if (!pread_block(key, off, m->block_size, leaf, leafblk, m->block_size)) {
                rc = -8; goto out;
            }
            for (uint32_t j = 0; j < ptrs; ++j) {
                uint32_t b = ((uint32_t*)leafblk)[j];
                FREE_CTX_BLOCK(b, -9);
            }
            FREE_CTX_BLOCK(leaf, -10);
        }
        FREE_CTX_BLOCK(file->i_block[13], -11);
    }

    if (ext2_alloc_ctx_commit(key, off, m, &fctx) != 0) rc = -12;

out:
    ext2_alloc_ctx_destroy(&fctx);
    return rc;

#undef FREE_CTX_BLOCK
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
    if (split_parent_name(path, parent, sizeof parent, name, sizeof name) != 0) return -3;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -4;

    uint32_t parent_ino = 0, ino = 0; uint8_t type = 0;
    if (ext2_resolve_path_at(key, off, &m, parent, &parent_ino) != 0) return -5;
    if (ext2_find_entry_at(key, off, &m, parent_ino, name, &ino, &type) != 1 || !ino) return -6;
    ext2_inode victim;
    if (ext2_read_inode_at(key, off, &m, ino, &victim) != 0) return -7;
    if ((victim.i_mode & 0170000) == 0040000 || type == 2) return -8;
    if (victim.i_block[14]) return -9;

    ext2_inode pdir;
    if (ext2_read_inode_at(key, off, &m, parent_ino, &pdir) != 0) return -10;
    uint8_t blk[4096]; bool removed = false; size_t want = strlen(name);
    for (int bi = 0; bi < 12 && pdir.i_block[bi] && !removed; ++bi) {
        if (!pread_block(key, off, m.block_size, pdir.i_block[bi], blk, m.block_size)) return -11;
        uint32_t pos = 0, prev = UINT32_MAX;
        while (pos + 8u <= m.block_size) {
            ext2_dirent *de = (ext2_dirent*)(blk + pos);
            if (de->rec_len < 8u || pos + de->rec_len > m.block_size) break;
            if (de->inode && de->name_len == want && memcmp(de->name, name, want) == 0) {
                if (prev != UINT32_MAX) ((ext2_dirent*)(blk + prev))->rec_len += de->rec_len;
                else de->inode = 0;
                if (!pwrite_block(key, off, m.block_size, pdir.i_block[bi], blk, m.block_size)) return -12;
                removed = true; break;
            }
            prev = pos; pos += de->rec_len;
        }
    }
    if (!removed) return -13;
    if (ext2_free_inode_data_blocks(key, off, &m, &victim) != 0) return -14;
    memset(&victim, 0, sizeof victim); victim.i_dtime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, &m, ino, &victim) != 0) return -15;
    if (ext2_free_inode_num(key, off, &m, ino) != 0) return -16;
    pdir.i_mtime = pdir.i_ctime = (uint32_t)time(NULL);
    if (ext2_write_inode_at(key, off, &m, parent_ino, &pdir) != 0) return -17;
    return 0;
}

/* Truncate an existing regular file to zero bytes while keeping its inode and
   directory entry.  This is the EXT2 operation needed for O_TRUNC semantics. */
int ext2_truncate_at(const char *key, uint64_t off, const char *path, uint64_t size)
{
    if (!key || !path || size != 0) return -1;
    ext2_meta m;
    if (ext2_load_meta(key, off, &m) != 0) return -2;
    uint32_t ino = 0;
    if (ext2_resolve_path_at(key, off, &m, path, &ino) != 0 || !ino) return -3;
    ext2_inode file;
    if (ext2_read_inode_at(key, off, &m, ino, &file) != 0) return -4;
    if ((file.i_mode & 0170000) == 0040000) return -5;
    if (file.i_block[14]) return -6;
    if (ext2_free_inode_data_blocks(key, off, &m, &file) != 0) return -7;
    memset(file.i_block, 0, sizeof file.i_block);
    file.i_size = 0; file.i_blocks = 0;
    file.i_mtime = file.i_ctime = (uint32_t)time(NULL);
    return ext2_write_inode_at(key, off, &m, ino, &file);
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

    uint32_t *single = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    uint32_t *droot  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    uint32_t *dleaf  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    if (!single || !droot || !dleaf) {
        free(single); free(droot); free(dleaf);
        return -14;
    }

    ext2_alloc_ctx actx;
    if (ext2_alloc_ctx_init(&m, &actx) != 0) {
        free(single); free(droot); free(dleaf);
        return -14;
    }
    uint32_t *data_blocks = need_data ? (uint32_t*)calloc((size_t)need_data, sizeof(uint32_t)) : NULL;
    if (need_data && !data_blocks) {
        ext2_alloc_ctx_destroy(&actx);
        free(single); free(droot); free(dleaf);
        return -14;
    }

    memset(file.i_block, 0, sizeof file.i_block);
    file.i_size = (uint32_t)len;
    file.i_atime = file.i_ctime = file.i_mtime = (uint32_t)time(NULL);

    uint32_t dleaf_block = 0;
    uint32_t dleaf_index = UINT32_MAX;
    uint32_t allocated_blocks = 0;
    int rc = 0;

#define ALLOC_EXISTING_BLOCK(dst_) do { \
        (dst_) = ext2_alloc_block_ctx(key, off, &m, &actx); \
        if (!(dst_)) { rc = -15; goto fail_existing; } \
        allocated_blocks++; \
    } while (0)

    for (uint64_t lbn = 0; lbn < need_data; ++lbn) {
        uint32_t data_block = 0;
        ALLOC_EXISTING_BLOCK(data_block);

        data_blocks[lbn] = data_block;

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

    if (ext2_write_data_runs(key, off, &m, data, len, data_blocks, need_data) != 0) {
        rc = -21; goto fail_existing;
    }

    file.i_blocks = allocated_blocks * (m.block_size / 512u);
    if (ext2_alloc_ctx_commit(key, off, &m, &actx) != 0) {
        rc = -21; goto fail_existing;
    }
    if (ext2_write_inode_at(key, off, &m, ino, &file) != 0) {
        rc = -22; goto fail_existing;
    }


fail_existing:
    ext2_alloc_ctx_destroy(&actx);
    free(data_blocks);
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

    uint32_t ino = ext2_alloc_inode(key, off, &m);
    if (!ino) return -11;

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

    single = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    droot  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    dleaf  = (uint32_t*)calloc(ptrs, sizeof(uint32_t));
    if (!single || !droot || !dleaf) {
        free(single); free(droot); free(dleaf);
        return -12;
    }

    ext2_alloc_ctx actx;
    if (ext2_alloc_ctx_init(&m, &actx) != 0) {
        free(single); free(droot); free(dleaf);
        return -12;
    }
    uint32_t *data_blocks = need_data ? (uint32_t*)calloc((size_t)need_data, sizeof(uint32_t)) : NULL;
    if (need_data && !data_blocks) {
        ext2_alloc_ctx_destroy(&actx);
        free(single); free(droot); free(dleaf);
        return -12;
    }

#define ALLOC_BLOCK(dst_) do { \
        (dst_) = ext2_alloc_block_ctx(key, off, &m, &actx); \
        if (!(dst_)) { rc = -13; goto fail; } \
        allocated_blocks++; \
    } while (0)

    int rc = 0;

    for (uint64_t lbn = 0; lbn < need_data; ++lbn) {
        uint32_t data_block = 0;
        ALLOC_BLOCK(data_block);

        data_blocks[lbn] = data_block;

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

    if (ext2_write_data_runs(key, off, &m, data, len, data_blocks, need_data) != 0) {
        rc = -19; goto fail;
    }

    file.i_blocks = allocated_blocks * (m.block_size / 512u);

    if (ext2_alloc_ctx_commit(key, off, &m, &actx) != 0) {
        rc = -19; goto fail;
    }
    if (ext2_write_inode_at(key, off, &m, ino, &file) != 0) {
        rc = -20; goto fail;
    }

    if (ext2_append_dirent_at(key, off, &m, parent_ino, ino, 1, name) < 0) {
        rc = -21; goto fail;
    }
    rc = 0;

fail:
    ext2_alloc_ctx_destroy(&actx);
    free(data_blocks);
    free(single);
    free(droot);
    free(dleaf);
    return rc;

#undef ALLOC_BLOCK
}

