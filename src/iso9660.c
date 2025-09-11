// src/iso9660.c
#include "iso9660.h"
#include <string.h>

bool iso_mount(vblk_t *dev, iso9660_t *out_ctx) {
    if (!dev || !out_ctx) return false;
    out_ctx->dev = dev;
    out_ctx->lba_start = 0;
    out_ctx->block_size = 2048;
    return true;
}

bool iso_read_file_by_path(iso9660_t *ctx,
                           const char *path,
                           uint8_t *out_buf,
                           uint32_t out_cap,
                           uint32_t *out_len) {
    (void)ctx; (void)path; (void)out_buf; (void)out_cap;
    if (out_len) *out_len = 0;
    return false; // not implemented yet
}