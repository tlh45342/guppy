// src/blkio.c

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "virtio_blk.h"

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512u
#endif
#ifndef BLKIO_MAX_BATCH_SECTORS
#define BLKIO_MAX_BATCH_SECTORS 128u
#endif

bool blk_write_bytes(uint64_t off, uint32_t len, const void *src) {
    const uint8_t *p = (const uint8_t*)src;
    while (len) {
        uint64_t lba = off / 512u;
        uint32_t boff = (uint32_t)(off % 512u);
        uint8_t sec[512];

        // RMW for partial sectors
        if (boff != 0 || len < 512) {
            if (!virtio_blk_read(lba, 1, sec)) return false;
        }

        uint32_t chunk = 512u - boff; if (chunk > len) chunk = len;
        memcpy(sec + boff, p, chunk);

        if (!virtio_blk_write(lba, 1, sec)) return false;

        p   += chunk;
        off += chunk;
        len -= chunk;
    }
    return true;
}

// src/blkio.c  (add this read function if it's missing)
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "virtio_blk.h"

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512u
#endif
#ifndef BLKIO_MAX_BATCH_SECTORS
#define BLKIO_MAX_BATCH_SECTORS 128u
#endif

bool blk_read_bytes(uint64_t off, uint32_t len, void *dst) {
    uint8_t *p = (uint8_t*)dst;
    if (len == 0) return true;

    while (len > 0) {
        uint64_t lba  = off / SECTOR_SIZE;
        uint32_t boff = (uint32_t)(off % SECTOR_SIZE);

        // Fast path: aligned & at least a full sector
        if (boff == 0 && len >= SECTOR_SIZE) {
            uint32_t full = len / SECTOR_SIZE;
            if (full > BLKIO_MAX_BATCH_SECTORS) full = BLKIO_MAX_BATCH_SECTORS;
            if (!virtio_blk_read(lba, full, p)) return false;

            uint32_t bytes = full * SECTOR_SIZE;
            p   += bytes;
            off += bytes;
            len -= bytes;
            continue;
        }

        // RMW path for partial head/tail sector: read one sector, copy slice
        uint8_t sec[SECTOR_SIZE];
        if (!virtio_blk_read(lba, 1, sec)) return false;

        uint32_t chunk = SECTOR_SIZE - boff;
        if (chunk > len) chunk = len;
        memcpy(p, sec + boff, chunk);

        p   += chunk;
        off += chunk;
        len -= chunk;
    }
    return true;
}

/* keep your existing blk_write_bytes(...) here (using virtio_blk_read for RMW) */
