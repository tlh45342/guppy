/* SPDX-License-Identifier: MIT
 *
 * guppy: minimal ext2 (read-first, tiny-write later) over a raw block device
 * - hosted C11 build for guppy (uses libc stdio/strings)
 *
 * build knobs:
 *   EXT2_DEBUG            : 1 to enable stderr logs, 0 to silence
 *   EXT2_MAX_BLOCK_SIZE   : safety cap for stack buffers (default 4096)
 */

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error "ext2.c requires C11 (_Static_assert, fixed-width integers)."
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>     /* size_t */
#include <stdio.h>      /* stderr logging */
#include <string.h>     /* memset, memcpy, strlen */
#include <inttypes.h>   /* PRIx32 */

#include "ext2.h"       /* public API + on-disk structs (packed) */
#include "blkio.h"      /* blk_read_bytes / blk_write_bytes shims */

/* ---------- config + logging ---------- */

#ifndef EXT2_DEBUG
#define EXT2_DEBUG 1
#endif

#if EXT2_DEBUG
  #define ELOG(S)       do { fputs((S), stderr); } while (0)
  #define EHEX32(X)     do { fprintf(stderr, "%08" PRIx32, (uint32_t)(X)); } while (0)
#else
  #define ELOG(S)       do{}while(0)
  #define EHEX32(X)     do{}while(0)
#endif

#ifndef EXT2_MAX_BLOCK_SIZE
#define EXT2_MAX_BLOCK_SIZE 4096u  /* increase if you’ll see 8K ext2 blocks */
#endif

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512u
#endif

/* --- local ext2 state --- */
static ext2_superblock sb;
static uint32_t g_block_size = 1024;
static uint32_t g_inode_size = 128;

/* --- forward decls --- */
static bool get_gd(uint32_t group_idx, ext2_group_desc *out);
static bool read_inode(uint32_t ino, ext2_inode *out);
static bool write_inode(uint32_t ino, const ext2_inode *in);

/* ---------- compile-time sanity checks ---------- */

_Static_assert(sizeof(ext2_group_desc) >= 32, "ext2_group_desc too small");
_Static_assert(sizeof(ext2_inode)      >= 128, "ext2_inode too small");
_Static_assert(EXT2_MAX_BLOCK_SIZE % SECTOR_SIZE == 0, "block size cap must be sector-multiple");

/* ---------- helpers ---------- */

static bool read_block(uint32_t bno, void *buf) {
    return blk_read_bytes((uint64_t)bno * g_block_size, g_block_size, buf);
}
static bool write_block(uint32_t bno, const void *buf) {
    return blk_write_bytes((uint64_t)bno * g_block_size, g_block_size, buf);
}

static bool get_gd(uint32_t group_idx, ext2_group_desc *out) {
    /* In ext2, group desc table starts at:
       - block 2 (offset 2048) if block size is 1024
       - otherwise at the start of block 1 (offset = block_size)
     */
    uint64_t gd_off = (g_block_size == 1024) ? 2048ull : (uint64_t)g_block_size;
    gd_off += (uint64_t)group_idx * sizeof(ext2_group_desc);
    return blk_read_bytes(gd_off, (uint32_t)sizeof(*out), out);
}

static bool read_inode(uint32_t ino, ext2_inode *out) {
    if (ino == 0) return false;
    uint32_t group = (ino - 1) / sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % sb.s_inodes_per_group;

    ext2_group_desc gd;
    if (!get_gd(group, &gd)) return false;

    uint64_t off = (uint64_t)gd.bg_inode_table * g_block_size
                 + (uint64_t)index * g_inode_size;

    if (g_inode_size > sizeof(*out)) return false; /* defensive bound */
    memset(out, 0, sizeof(*out));
    return blk_read_bytes(off, g_inode_size, out);
}

static bool write_inode(uint32_t ino, const ext2_inode *in) {
    if (ino == 0) return false;
    uint32_t group = (ino - 1) / sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % sb.s_inodes_per_group;

    ext2_group_desc gd;
    if (!get_gd(group, &gd)) return false;

    uint64_t off = (uint64_t)gd.bg_inode_table * g_block_size
                 + (uint64_t)index * g_inode_size;

    uint32_t wlen = (g_inode_size < sizeof(*in)) ? g_inode_size : (uint32_t)sizeof(*in);
    return blk_write_bytes(off, wlen, in);
}

static bool bitmap_get(const uint8_t *bm, uint32_t idx) {
    return (bm[idx >> 3] >> (idx & 7)) & 1u;
}
static void bitmap_set(uint8_t *bm, uint32_t idx) {
    bm[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}

static bool alloc_block(uint32_t *out_bno) {
    /* MVP: group 0 only */
    ext2_group_desc gd;
    if (!get_gd(0, &gd)) return false;

    uint8_t bm[EXT2_MAX_BLOCK_SIZE];
    if (g_block_size > sizeof(bm)) return false;

    /* block bitmap is at gd.bg_block_bitmap (block-number) */
    if (!read_block(gd.bg_block_bitmap, bm)) return false;

    for (uint32_t i = 0; i < sb.s_blocks_per_group; ++i) {
        if (!bitmap_get(bm, i)) {
            bitmap_set(bm, i);
            if (!write_block(gd.bg_block_bitmap, bm)) return false;

            *out_bno = sb.s_first_data_block + i;  /* typical mapping */
            /* TODO: dec free block counters and write back gd/sb */
            return true;
        }
    }
    return false;
}

static bool alloc_inode(uint32_t *out_ino) {
    /* MVP: group 0 only */
    ext2_group_desc gd;
    if (!get_gd(0, &gd)) return false;

    uint8_t ibm[EXT2_MAX_BLOCK_SIZE];
    if (g_block_size > sizeof(ibm)) return false;

    /* inode bitmap at gd.bg_inode_bitmap */
    if (!read_block(gd.bg_inode_bitmap, ibm)) return false;

    for (uint32_t i = 0; i < sb.s_inodes_per_group; ++i) {
        if (!bitmap_get(ibm, i)) {
            bitmap_set(ibm, i);
            if (!write_block(gd.bg_inode_bitmap, ibm)) return false;

            *out_ino = 1 + i; /* inode numbers are 1-based */
            /* TODO: dec free inode counters, write back gd/sb */
            return true;
        }
    }
    return false; /* out of inodes */
}

static bool dir_add_entry(uint32_t dir_ino, const char *name, uint32_t child_ino, uint8_t ftype) {
    /* read dir inode */
    ext2_inode dir;
    if (!read_inode(dir_ino, &dir)) return false;

    uint8_t blk[EXT2_MAX_BLOCK_SIZE];
    if (g_block_size > sizeof(blk)) return false;

    /* scan direct blocks for space; if none, alloc a new block and attach */
    for (int i = 0; i < 12; i++) {
        if (dir.i_block[i] == 0) {
            /* avoid taking address of packed member: use temp */
            uint32_t newb;
            if (!alloc_block(&newb)) return false;
            dir.i_block[i] = newb;

            memset(blk, 0, g_block_size);
            if (!write_block(dir.i_block[i], blk)) return false;
            dir.i_size += g_block_size;
            if (!write_inode(dir_ino, &dir)) return false;
        }

        if (!read_block(dir.i_block[i], blk)) return false;

        uint32_t off = 0;
        const uint32_t need = 8u + (((uint32_t)strlen(name) + 3u) & ~3u);

        while (off + 8u <= g_block_size) {
            ext2_dirent *de = (ext2_dirent *)(blk + off);

            /* empty area (fresh block or unused tail) */
            if (de->rec_len == 0u) {
                if (need > g_block_size - off) break;

                de->inode     = child_ino;
                de->rec_len   = (uint16_t)(g_block_size - off);
                de->name_len  = (uint8_t)strlen(name);
                de->file_type = ftype; /* 1=file, 2=dir */
                memcpy(de->name, name, de->name_len);

                return write_block(dir.i_block[i], blk);
            }

            /* split last entry if enough slack space */
            if (off + (uint32_t)de->rec_len <= g_block_size) {
                uint32_t ideal = 8u + ((((uint32_t)de->name_len) + 3u) & ~3u);
                uint32_t free_bytes = (uint32_t)de->rec_len - ideal;

                if (free_bytes >= need) {
                    uint16_t old_rec = de->rec_len;
                    de->rec_len = (uint16_t)ideal;

                    ext2_dirent *ne = (ext2_dirent *)((uint8_t *)de + ideal);
                    ne->inode     = child_ino;
                    ne->rec_len   = (uint16_t)(old_rec - (uint16_t)ideal);
                    ne->name_len  = (uint8_t)strlen(name);
                    ne->file_type = ftype;
                    memcpy(ne->name, name, ne->name_len);

                    return write_block(dir.i_block[i], blk);
                }
            }

            off += (uint32_t)de->rec_len;
        }
    }
    return false;
}

/* ---------- public API ---------- */

bool ext2_create_and_write(const char *path, const uint8_t *data, uint32_t len) {
    /* MVP: only root ("/name"); split parent later */
    if (!path || path[0] != '/' || strchr(path + 1, '/')) return false;
    const char *name = path + 1;

    /* alloc inode */
    uint32_t ino;
    if (!alloc_inode(&ino)) return false;

    /* fill inode (regular file) */
    ext2_inode in;
    memset(&in, 0, sizeof(in));
    in.i_mode        = (uint16_t)(0x8000 | 0644);
    in.i_size        = len;
    in.i_links_count = 1;

    /* allocate data blocks and write (direct blocks only for MVP) */
    uint32_t bs = g_block_size;
    uint32_t blocks = (len + bs - 1u) / bs;
    if (blocks > 12u) return false;

    uint32_t written = 0;
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t bno;
        if (!alloc_block(&bno)) return false;
        in.i_block[i] = bno;

        uint8_t buf[EXT2_MAX_BLOCK_SIZE];
        if (bs > sizeof(buf)) return false;

        uint32_t chunk = (len - written < bs) ? (len - written) : bs;
        memset(buf, 0, bs);
        memcpy(buf, data + written, chunk);

        if (!write_block(bno, buf)) return false;
        written += chunk;
    }

    if (!write_inode(ino, &in)) return false;

    /* link into root directory (#2) */
    if (!dir_add_entry(2u, name, ino, 1u)) return false;

    return true;
}
