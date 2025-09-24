// src/vblk.c

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

/* Lower-level I/O: adjust this extern if your disk layer has a different name.
   Expected semantics: read LEN bytes from device identified by DEVKEY at byte
   offset OFF into DST. Return true on success. */
extern bool diskio_pread(const char *devkey, uint64_t off, void *dst, uint32_t len);

/*------------------------------------------------------------------------------*
 * Global registry
 *------------------------------------------------------------------------------*/
vblk_t g_vblk[VBLK_MAX];
int    g_vblk_count = 0;

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

const vblk_t *vblk_by_name(const char *name) {
    return find_by_name(name, NULL);
}

int vblk_register(const vblk_t *entry) {
    if (!entry || entry->name[0] == '\0') return -1;

    int idx = -1;
    const vblk_t *existing = find_by_name(entry->name, &idx);
    if (existing) {
        g_vblk[idx] = *entry;  // replace in place
        return idx;
    }

    if (g_vblk_count >= VBLK_MAX) return -1;
    g_vblk[g_vblk_count] = *entry;
    return g_vblk_count++;
}

void vblk_clear(void) {
    memset(g_vblk, 0, sizeof(g_vblk));
    g_vblk_count = 0;
}

/*------------------------------------------------------------------------------*
 * Canonical read API
 *------------------------------------------------------------------------------*/

static inline uint64_t part_bytes_limit(const vblk_t *dev) {
    // If lba_size==0, treat as "no explicit limit" (whole disk or unknown)
    return (dev->lba_size == 0) ? UINT64_MAX
                                : (dev->lba_size * (uint64_t)LSEC);
}

bool vblk_read_bytes(vblk_t *dev, uint64_t off, uint32_t len, void *dst) {
    if (!dev || !dst) return false;
    uint64_t limit = (dev->lba_size == 0) ? UINT64_MAX : dev->lba_size * 512ull;
    if (off > limit || (uint64_t)len > limit - off) return false;

    uint64_t abs_off = dev->lba_start * 512ull + off;
    const char *key = dev->dev[0] ? dev->dev : dev->name;
    if (!diskio_pread(key, abs_off, dst, len)) {
        fprintf(stderr, "vblk: read failed on %s @+%" PRIu64 " (%u bytes)\n", key, abs_off, len);
        return false;
    }
    return true;
}

bool vblk_read_blocks(vblk_t *dev, uint32_t lba, uint32_t count, void *dst) {
    if (!dev || !dst) return false;

    uint64_t off = (uint64_t)lba * (uint64_t)LSEC;
    uint64_t bytes = (uint64_t)count * (uint64_t)LSEC;

    // Clamp/validate against partition size if known
    uint64_t limit = part_bytes_limit(dev);
    if (off > limit) return false;
    if (bytes > limit - off) return false;

    // Absolute byte offset on backing device
    uint64_t abs_off = dev->lba_start * (uint64_t)LSEC;
    abs_off += off;

    // Safe to cast bytes to uint32_t because count is uint32_t and LSEC is 512
    return diskio_pread(dev->dev[0] ? dev->dev : dev->name, abs_off, dst, (uint32_t)bytes);
}

bool vblk_resolve_to_base(const char *name,
                          char *key_out, size_t key_sz,
                          uint64_t *base_off_bytes,
                          uint64_t *length_bytes)
{
    if (!name || !key_out || key_sz==0) return false;
    const vblk_t *vb = vblk_by_name(name);
    if (!vb) return false;

    const char *key = vb->dev[0] ? vb->dev : vb->name;   // parent key
    const char *resolved = diskio_resolve(key);
    if (!resolved) return false;

    snprintf(key_out, key_sz, "%s", resolved);

    uint64_t off = vb->lba_start * 512ull;
    if (base_off_bytes) *base_off_bytes = off;

    uint64_t len = 0;
    if (vb->lba_size) {
        len = vb->lba_size * 512ull;
    } else {
        // unknown size in table → derive from backing file size when possible
        uint64_t total = diskio_size_bytes(resolved);
        if (total > off) len = total - off;
    }
    if (length_bytes) *length_bytes = len;

    return true;
}

static inline const char* base_of(const char *s) {
    return (s && strncmp(s, "/dev/", 5) == 0) ? (s + 5) : s;
}

vblk_t *vblk_open(const char *key) {
    DBG("vblk_open: key='%s'", key ? key : "(null)");
    if (!key || !*key) return NULL;

    /* 1) Try exact match on internal key (name) OR display path (dev) */
    for (int i = 0; i < g_vblk_count; ++i) {
        vblk_t *e = &g_vblk[i];
        if (!e->name[0]) continue;

        if (strcmp(e->name, key) == 0 || (e->dev[0] && strcmp(e->dev, key) == 0)) {
            if (e->lba_size == 0) { DBG("vblk_open: reject '%s' (size=0)", key); return NULL; }
            DBG("vblk_open: hit name='%s' dev='%s' size=%" PRIu64, e->name, e->dev, e->lba_size);
            return e;
        }
    }

    /* 2) If caller passed /dev/..., retry with basename */
    const char *base = base_of(key);
    if (base != key) {
        DBG("vblk_open: retry with base='%s'", base);
        for (int i = 0; i < g_vblk_count; ++i) {
            vblk_t *e = &g_vblk[i];
            if (strcmp(e->name, base) == 0) {
                if (e->lba_size == 0) { DBG("vblk_open: reject base '%s' (size=0)", base); return NULL; }
                DBG("vblk_open: hit name='%s' dev='%s' size=%" PRIu64, e->name, e->dev, e->lba_size);
                return e;
            }
        }
    }

    DBG("vblk_open: not found");
    return NULL;
}

/* If you have nothing to release, close can be a no-op for now. */
void vblk_close(vblk_t *dev) {
    (void)dev;
    DBG("vblk_close: %p", (void*)dev);
}