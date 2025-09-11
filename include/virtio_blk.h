// include/virtio.h

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Sector-based I/O interface (count = number of 512B sectors). */
bool virtio_blk_read (uint64_t lba, size_t count, void *dst);
bool virtio_blk_write(uint64_t lba, size_t count, const void *src);
