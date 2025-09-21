// src/devmap.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "devmap.h"

/* Your blk layer should expose this. It maps an image into the global
   blk address space and returns the absolute base LBA (512B units) and
   the LBA count for the mapping. Implement it in blkio.c if it doesn't
   already exist. */
extern bool blkio_map_image(const char *path,
                            uint64_t *out_lba_base,
                            uint64_t *out_lba_count);

#ifndef DEVMAP_MAX
#define DEVMAP_MAX 32
#endif

typedef struct {
    char     dev[32];        // "/dev/b"
    char     path[256];      // "disc.iso"
    uint64_t lba_base;       // absolute base in 512B LBAs
    uint64_t lba_count;      // length in 512B LBAs
    bool     in_use;
} devmap_entry_t;

static devmap_entry_t g_map[DEVMAP_MAX];

static devmap_entry_t* find_slot(const char *dev) {
    for (int i = 0; i < DEVMAP_MAX; ++i) {
        if (g_map[i].in_use && strcmp(g_map[i].dev, dev) == 0) return &g_map[i];
    }
    return NULL;
}
static devmap_entry_t* find_free(void) {
    for (int i = 0; i < DEVMAP_MAX; ++i) {
        if (!g_map[i].in_use) return &g_map[i];
    }
    return NULL;
}

bool devmap_add(const char *dev, const char *image_path) {
    if (!dev || !image_path) return false;

    /* Map into blk space and get absolute base/count (512B LBAs). */
    uint64_t base = 0, count = 0;
    if (!blkio_map_image(image_path, &base, &count)) {
        fprintf(stderr, "devmap: failed to map image %s\n", image_path);
        return false;
    }

    devmap_entry_t *e = find_slot(dev);
    if (!e) e = find_free();
    if (!e) { fprintf(stderr, "devmap: table full\n"); return false; }

    memset(e, 0, sizeof(*e));
    snprintf(e->dev,  sizeof(e->dev),  "%s", dev);
    snprintf(e->path, sizeof(e->path), "%s", image_path);
    e->lba_base  = base;
    e->lba_count = count;
    e->in_use    = true;
    return true;
}

const char *devmap_resolve(const char *dev) {
    const devmap_entry_t *e = find_slot(dev);
    return e ? e->path : NULL;
}

bool devmap_query_range(const char *dev, uint64_t *out_base, uint64_t *out_count) {
    const devmap_entry_t *e = find_slot(dev);
    if (!e) return false;
    if (out_base)  *out_base  = e->lba_base;
    if (out_count) *out_count = e->lba_count;
    return true;
}

void devmap_list(void) {
    for (int i = 0; i < DEVMAP_MAX; ++i) {
        if (!g_map[i].in_use) continue;
        printf("%-8s  %-24s  base=%llu  size=%llu LBAs\n",
               g_map[i].dev, g_map[i].path,
               (unsigned long long)g_map[i].lba_base,
               (unsigned long long)g_map[i].lba_count);
    }
}
