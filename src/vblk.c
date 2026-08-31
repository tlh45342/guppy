#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "debug.h"
#include "vblk.h"
#include "diskio.h"

#ifndef VBLK_MAX
#define VBLK_MAX 256
#endif
#define LSEC 512u

vblk_t g_vblk[VBLK_MAX];
int g_vblk_count = 0;

static const vblk_t *find_by_name(const char *name, int *out_index) {
    for (int i = 0; i < g_vblk_count; ++i) {
        if (strcmp(g_vblk[i].name, name) == 0) {
            if (out_index) *out_index = i;
            return &g_vblk[i];
        }
    }
    if (out_index) *out_index = -1;
    return NULL;
}

const vblk_t *vblk_by_name(const char *name) { return find_by_name(name, NULL); }

int vblk_register(const vblk_t *entry) {
    if (!entry || entry->name[0] == '\0') return -1;
    int idx = -1;
    const vblk_t *existing = find_by_name(entry->name, &idx);
    if (existing) { g_vblk[idx] = *entry; return idx; }
    if (g_vblk_count >= VBLK_MAX) return -1;
    g_vblk[g_vblk_count] = *entry;
    return g_vblk_count++;
}

void vblk_clear(void) { memset(g_vblk, 0, sizeof g_vblk); g_vblk_count = 0; }

static uint64_t vblk_limit_bytes(const vblk_t *dev) {
    return (!dev || dev->lba_size == 0) ? UINT64_MAX : dev->lba_size * (uint64_t)LSEC;
}

static const char *vblk_backing_key(const vblk_t *dev) {
    return dev->dev[0] ? dev->dev : dev->name;
}

bool vblk_read_bytes(vblk_t *dev, uint64_t off, uint32_t len, void *dst) {
    if (!dev || !dst) return false;
    uint64_t limit = vblk_limit_bytes(dev);
    if (off > limit || (uint64_t)len > limit - off) return false;
    uint64_t abs_off = dev->lba_start * (uint64_t)LSEC + off;
    const char *key = vblk_backing_key(dev);
    if (!diskio_pread(key, abs_off, dst, len)) {
        fprintf(stderr, "vblk: read failed on %s @+%" PRIu64 " (%u bytes)\n", key, abs_off, len);
        return false;
    }
    return true;
}

bool vblk_write_bytes(vblk_t *dev, uint64_t off, uint32_t len, const void *src) {
    if (!dev || !src || dev->ro) return false;
    uint64_t limit = vblk_limit_bytes(dev);
    if (off > limit || (uint64_t)len > limit - off) return false;
    uint64_t abs_off = dev->lba_start * (uint64_t)LSEC + off;
    const char *key = vblk_backing_key(dev);
    if (!diskio_pwrite(key, abs_off, src, len)) {
        fprintf(stderr, "vblk: write failed on %s @+%" PRIu64 " (%u bytes)\n", key, abs_off, len);
        return false;
    }
    return true;
}

bool vblk_read_blocks(vblk_t *dev, uint64_t lba, uint32_t count, void *dst) {
    if (!dev || !dst || count == 0) return false;
    uint32_t bsz = dev->block_bytes ? dev->block_bytes : LSEC;
    if (lba > UINT64_MAX / bsz) return false;
    uint64_t off = lba * (uint64_t)bsz; /* device-relative: do NOT add lba_start here */
    uint64_t len = (uint64_t)count * bsz;
    uint8_t *p = (uint8_t*)dst;
    while (len) {
        uint32_t step = len > UINT32_MAX ? UINT32_MAX : (uint32_t)len;
        if (!vblk_read_bytes(dev, off, step, p)) return false;
        off += step; p += step; len -= step;
    }
    return true;
}

bool vblk_write_blocks(vblk_t *dev, uint64_t lba, uint32_t count, const void *src) {
    if (!dev || !src || count == 0 || dev->ro) return false;
    uint32_t bsz = dev->block_bytes ? dev->block_bytes : LSEC;
    if (lba > UINT64_MAX / bsz) return false;
    uint64_t off = lba * (uint64_t)bsz;
    uint64_t len = (uint64_t)count * bsz;
    const uint8_t *p = (const uint8_t*)src;
    while (len) {
        uint32_t step = len > UINT32_MAX ? UINT32_MAX : (uint32_t)len;
        if (!vblk_write_bytes(dev, off, step, p)) return false;
        off += step; p += step; len -= step;
    }
    return true;
}

uint64_t vblk_size_bytes(vblk_t *dev) {
    if (!dev) return 0;
    if (dev->lba_size) return dev->lba_size * (uint64_t)LSEC;
    const char *key = vblk_backing_key(dev);
    uint64_t total = diskio_size_bytes(key);
    uint64_t base = dev->lba_start * (uint64_t)LSEC;
    return total > base ? total - base : 0;
}

bool vblk_flush(vblk_t *dev) {
    if (!dev) return false;
    return diskio_flush(vblk_backing_key(dev));
}

bool vblk_resolve_to_base(const char *name, char *key_out, size_t key_sz,
                          uint64_t *base_off_bytes, uint64_t *length_bytes) {
    if (!name || !key_out || key_sz == 0) return false;
    const vblk_t *vb = vblk_by_name(name);
    if (!vb) return false;
    const char *key = vblk_backing_key(vb);
    const char *resolved = diskio_resolve(key);
    if (!resolved) return false;
    snprintf(key_out, key_sz, "%s", resolved);
    uint64_t off = vb->lba_start * (uint64_t)LSEC;
    if (base_off_bytes) *base_off_bytes = off;
    if (length_bytes) *length_bytes = vb->lba_size ? vb->lba_size * (uint64_t)LSEC : vblk_size_bytes((vblk_t*)vb);
    return true;
}

static const char *base_of(const char *s) { return (s && strncmp(s, "/dev/", 5) == 0) ? s + 5 : s; }

vblk_t *vblk_open(const char *key) {
    DBG("vblk_open: key='%s'", key ? key : "(null)");
    if (!key || !*key) return NULL;
    for (int i = 0; i < g_vblk_count; ++i) {
        vblk_t *e = &g_vblk[i];
        if (!e->name[0]) continue;
        if (strcmp(e->name, key) == 0 || (e->dev[0] && strcmp(e->dev, key) == 0)) {
            if (e->lba_size == 0) { DBG("vblk_open: reject '%s' (size=0)", key); return NULL; }
            return e;
        }
    }
    const char *base = base_of(key);
    if (base != key) {
        for (int i = 0; i < g_vblk_count; ++i) {
            vblk_t *e = &g_vblk[i];
            if (strcmp(e->name, base) == 0) {
                if (e->lba_size == 0) return NULL;
                return e;
            }
        }
    }
    return NULL;
}

void vblk_close(vblk_t *dev) { (void)dev; }
