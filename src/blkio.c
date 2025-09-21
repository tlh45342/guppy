// src/blkio.c — absolute-LBA block I/O over multiple image files
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "blkio.h"

#ifndef BLK_MAX_FILES
#define BLK_MAX_FILES 32
#endif

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512u
#endif

typedef struct {
    char     path[256];
    FILE    *fp;
    bool     writable;
    uint64_t base_lba;     // absolute base (512B LBAs)
    uint64_t lba_count;    // length (LBAs)
    bool     in_use;
} blk_file_t;

static blk_file_t g_files[BLK_MAX_FILES];
static uint64_t   g_next_base_lba = 0;   // simple bump allocator

/* 64-bit seek helper (MSVC uses _fseeki64; others use fseeko).
   Caller passes byte offset from start of file. */
static inline int file_seek64(FILE *fp, uint64_t off) {
#if defined(_MSC_VER)
    return _fseeki64(fp, (int64_t)off, SEEK_SET);
#else
    return fseeko(fp, (off_t)off, SEEK_SET);
#endif
}

/* Find the mapped file that contains absolute byte offset `abs_off`.
   Returns the entry and sets: file_off = offset inside that file,
   avail = bytes remaining in that file from file_off to end of mapping (<= UINT32_MAX). */
static blk_file_t* find_file_for_abs(uint64_t abs_off, uint64_t *out_file_off, uint32_t *out_avail) {
    for (int i = 0; i < BLK_MAX_FILES; ++i) {
        if (!g_files[i].in_use) continue;
        uint64_t start = g_files[i].base_lba * (uint64_t)SECTOR_SIZE;
        uint64_t end   = start + g_files[i].lba_count * (uint64_t)SECTOR_SIZE;
        if (abs_off >= start && abs_off < end) {
            if (out_file_off) *out_file_off = abs_off - start;
            if (out_avail) {
                uint64_t avail = end - abs_off;
                *out_avail = (avail > UINT32_MAX) ? UINT32_MAX : (uint32_t)avail;
            }
            return &g_files[i];
        }
    }
    return NULL;
}

/* Map an image into the global absolute LBA space and return (base_lba, lba_count). */
bool blkio_map_image(const char *path, uint64_t *out_lba_base, uint64_t *out_lba_count) {
    if (!path) return false;

    // Open writeable if possible; fall back to read-only.
    FILE *fp = fopen(path, "rb+");
    bool writable = true;
    if (!fp) { fp = fopen(path, "rb"); writable = false; }
    if (!fp) {
        fprintf(stderr, "blkio: fopen(%s) failed: %s\n", path, strerror(errno));
        return false;
    }

    // Determine file size (64-bit safe)
#if defined(_MSC_VER)
    if (_fseeki64(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    int64_t size = _ftelli64(fp);
    if (size < 0) { fclose(fp); return false; }
    if (_fseeki64(fp, 0, SEEK_SET) != 0) { fclose(fp); return false; }
#else
    if (fseeko(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    off_t size = ftello(fp);
    if (size < 0) { fclose(fp); return false; }
    if (fseeko(fp, 0, SEEK_SET) != 0) { fclose(fp); return false; }
#endif

    uint64_t lba_count = ((uint64_t)size + (SECTOR_SIZE - 1)) / SECTOR_SIZE;

    // Allocate a slot
    int slot = -1;
    for (int i = 0; i < BLK_MAX_FILES; ++i) {
        if (!g_files[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        fclose(fp);
        fprintf(stderr, "blkio: table full\n");
        return false;
    }

    blk_file_t *e = &g_files[slot];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->fp        = fp;
    e->writable  = writable;
    e->base_lba  = g_next_base_lba;
    e->lba_count = lba_count;
    e->in_use    = true;

    if (out_lba_base)  *out_lba_base  = e->base_lba;
    if (out_lba_count) *out_lba_count = e->lba_count;

    g_next_base_lba += lba_count;
    return true;
}

/* Read bytes at absolute address across mapped images. */
bool blk_read_bytes(uint64_t abs, uint32_t len, void *dst) {
    if (!dst || len == 0) return false;

    uint8_t *out = (uint8_t *)dst;
    uint32_t remaining = len;

    while (remaining) {
        uint64_t file_off = 0;
        uint32_t avail = 0;
        blk_file_t *e = find_file_for_abs(abs, &file_off, &avail);
        if (!e) {
            fprintf(stderr, "blkio: unmapped read abs=%llu len=%u\n",
                    (unsigned long long)abs, (unsigned)remaining);
            return false;
        }
        uint32_t chunk = (avail < remaining) ? avail : remaining;

        if (file_seek64(e->fp, file_off) != 0) return false;
        size_t got = fread(out, 1, chunk, e->fp);
        if (got != chunk) return false;

        abs       += chunk;
        out       += chunk;
        remaining -= chunk;
    }
    return true;
}

/* Write bytes at absolute address across mapped images (only if image was opened writable). */
bool blk_write_bytes(uint64_t abs, uint32_t len, const void *src) {
    if (!src || len == 0) return false;

    const uint8_t *in = (const uint8_t *)src;
    uint32_t remaining = len;

    while (remaining) {
        uint64_t file_off = 0;
        uint32_t avail = 0;
        blk_file_t *e = find_file_for_abs(abs, &file_off, &avail);
        if (!e) {
            fprintf(stderr, "blkio: unmapped write abs=%llu len=%u\n",
                    (unsigned long long)abs, (unsigned)remaining);
            return false;
        }
        if (!e->writable) {
            fprintf(stderr, "blkio: write to read-only image %s\n", e->path);
            return false;
        }
        uint32_t chunk = (avail < remaining) ? avail : remaining;

        if (file_seek64(e->fp, file_off) != 0) return false;
        size_t put = fwrite(in, 1, chunk, e->fp);
        if (put != chunk) return false;

        abs       += chunk;
        in        += chunk;
        remaining -= chunk;
    }
    return true;
}
