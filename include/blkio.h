// include/blkio.h

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Byte-addressed I/O over the backing block device/image.
   Offsets are absolute bytes; len is bytes. */
bool blk_read_bytes (uint64_t off, uint32_t len, void *dst);
bool blk_write_bytes(uint64_t off, uint32_t len, const void *src);
bool blkio_map_image(const char *path, uint64_t *out_lba_base, uint64_t *out_lba_count);
