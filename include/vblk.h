#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A “virtual block” descriptor that ties a name to a backing device/partition. */
typedef struct vblk {
    char     name[32];      /* e.g., "/dev/a1", "root", etc. */
    char     dev[32];       /* backing device key (e.g., "/dev/a") */
    int      part_index;    /* partition index on dev (or -1 for whole disk) */
    char     fstype[16];    /* "gpt", "mbr", "ext2", "-" if unknown */
    uint64_t lba_start;     /* starting LBA on the device */
    uint64_t lba_size;      /* size in LBAs */
} vblk_t;

/* Global table (owned/defined in vblk.c). */
extern vblk_t g_vblk[];
extern int    g_vblk_count;

/* Look up by human-readable name (returns NULL if not found). */
const vblk_t *vblk_by_name(const char *name);

/* (Optional helpers you may want) */
int  vblk_register(const vblk_t *entry);  /* returns index or -1 on full */
void vblk_clear(void);

#ifdef __cplusplus
}
#endif
