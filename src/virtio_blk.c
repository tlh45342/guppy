// src/virtio_blk.c

#include "virtio_blk.h"
#include "blkio.h"     // blk_read_bytes / blk_write_bytes
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef VIRTIO_SECTOR_SIZE
#define VIRTIO_SECTOR_SIZE 512u
#endif

bool virtio_blk_read(uint64_t lba, size_t count, void *dst) {
    if (!dst || count == 0) return false;
    uint64_t off = lba * (uint64_t)VIRTIO_SECTOR_SIZE;
    uint64_t len = (uint64_t)count * (uint64_t)VIRTIO_SECTOR_SIZE;
    if (len > 0xFFFFFFFFu) return false;  // guard if blk_* take uint32_t
    return blk_read_bytes(off, (uint32_t)len, dst);
}

bool virtio_blk_write(uint64_t lba, size_t count, const void *src) {
    if (!src || count == 0) return false;
    uint64_t off = lba * (uint64_t)VIRTIO_SECTOR_SIZE;
    uint64_t len = (uint64_t)count * (uint64_t)VIRTIO_SECTOR_SIZE;
    if (len > 0xFFFFFFFFu) return false;  // guard if blk_* take uint32_t
    return blk_write_bytes(off, (uint32_t)len, src);
}
