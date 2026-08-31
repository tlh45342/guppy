// vblh.h

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define VBLK_NAME_LEN 32
#define VBLK_DEV_LEN  32
#define VBLK_FST_LEN  16

typedef struct vblk {
    char     name[VBLK_NAME_LEN];
    char     dev[VBLK_DEV_LEN];
    int      part_index;
    char     fstype[VBLK_FST_LEN];
    uint64_t lba_start;
    uint64_t lba_size;
    uint32_t block_bytes;
    bool     ro;
} vblk_t;

extern vblk_t g_vblk[];
extern int    g_vblk_count;

const vblk_t *vblk_by_name(const char *name);
int  vblk_register(const vblk_t *entry);
void vblk_clear(void);

/* Canonical device-relative I/O API. Partition translation occurs here. */
bool vblk_read_bytes(vblk_t *dev, uint64_t off, uint32_t len, void *dst);
bool vblk_write_bytes(vblk_t *dev, uint64_t off, uint32_t len, const void *src);
bool vblk_read_blocks(vblk_t *dev, uint64_t lba, uint32_t count, void *dst);
bool vblk_write_blocks(vblk_t *dev, uint64_t lba, uint32_t count, const void *src);
uint64_t vblk_size_bytes(vblk_t *dev);
bool vblk_flush(vblk_t *dev);

/* Legacy escape hatch retained temporarily for callers not normalized yet. */
bool vblk_resolve_to_base(const char *name,
                          char *key_out, size_t key_sz,
                          uint64_t *base_off_bytes,
                          uint64_t *length_bytes);

vblk_t *vblk_open(const char *dev);
vblk_t *vblk_open_partition(const char *dev, int part_index);
void vblk_close(vblk_t *blk);
int  blkdev_open(const char *spec, vblk_t **out);
void blkdev_close(vblk_t *dev);
