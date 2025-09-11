#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "vblk.h"   /* for vblk_t */

/* Minimal concrete ISO9660 context so callers can place it on the stack. */
typedef struct iso9660 {
    vblk_t  *dev;         /* backing device */
    uint64_t lba_start;   /* start of filesystem (often 0) */
    uint32_t block_size;  /* typically 2048 for ISO9660 */
    /* add fields as you implement more */
} iso9660_t;

/* Mount the ISO filesystem from a virtual block device into an iso9660_t handle. */
bool iso_mount(vblk_t *dev, iso9660_t *out_ctx);

/* Read a file from the ISO by absolute path (e.g. "/HELLO.TXT"). */
bool iso_read_file_by_path(iso9660_t *ctx,
                           const char *path,
                           uint8_t *out_buf,
                           uint32_t out_cap,
                           uint32_t *out_len);
