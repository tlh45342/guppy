#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>   // for size_t

/* vblk_t is a plain registry row (no ops/impl).
 * Reads are performed via vblk_read_* using dev/lba_start/lba_size. */
#define VBLK_NAME_LEN 32
#define VBLK_DEV_LEN  32
#define VBLK_FST_LEN  16

/* A “virtual block” descriptor that ties a name to a backing device/partition. */
typedef struct vblk {
    char     name[VBLK_NAME_LEN]; /* "/dev/a1", "root", etc. */
    char     dev [VBLK_DEV_LEN ]; /* backing device key (e.g., "/dev/a") */
    int      part_index;          /* partition index on dev (-1 = whole disk) */
    char     fstype[VBLK_FST_LEN];/* "gpt", "mbr", "ext2", "-" if unknown */
    uint64_t lba_start;           /* starting LBA on the device */
    uint64_t lba_size;            /* size in LBAs */
    uint32_t block_bytes;         /* NEW: logical block size for THIS vblk (e.g., 512, 2048) */
    bool     ro;                  /* NEW: read-only media? (CD/ISO=true) */
} vblk_t;

/* Global table (owned/defined in vblk.c). */
extern vblk_t g_vblk[];
extern int    g_vblk_count;

/* Lookup by human-readable name (returns NULL if not found). */
const vblk_t *vblk_by_name(const char *name);

/* Registry helpers */
int  vblk_register(const vblk_t *entry);  /* returns index or -1 on full */
void vblk_clear(void);

/* ------------------------------------------------------------------------------------- */

/* Canonical read API (these are what FS code should use). */

/**
 * Name: vblk_read_bytes 
 *
 * Read an arbitrary range of bytes from a virtual block device.
 *
 * Parameters:
 *   dev    - (INPUT) Virtual block device handle. Must be a valid, opened device.
 *   offset - (INPUT) Absolute byte offset from the start of the device/image at which
 *            to begin reading. This is NOT an LBA; it is a raw byte position.
 *   bytes  - (INPUT) Number of bytes to read. Must be > 0. The caller is responsible
 *            for ensuring (offset + bytes) does not exceed the device size.
 *   dst    - (OUTPUT) Caller-provided buffer with capacity for exactly 'bytes' bytes.
 *
 * Returns:
 *   true   - All requested bytes were read into 'dst'.
 *   false  - Any failure (invalid arguments, out-of-range request, device I/O error,
 *            or partial read). On 
 */

bool vblk_read_bytes  (vblk_t *dev, uint64_t off,  uint32_t len,   void *dst);

/**
 * Name: vblk_read_blocks
 *
 * Read one or more contiguous logical blocks from a virtual block device.
 *
 * Parameters:
 *   dev    - (INPUT) Virtual block device handle. Must be a valid, opened device.
 *   lba    - (INPUT) Starting Logical Block Address on the device, in device-
 *            logical units (e.g., 512-byte sectors). LBA 0 is the first block.
 *   count  - (INPUT) Number of blocks to read starting at 'lba'. Must be > 0.
 *   dst    - (OUTPUT) Caller-provided buffer large enough to hold
 *            (count * device_logical_block_bytes) bytes.
 *
 * Returns:
 *   true   - All requested blocks were read into 'dst'.
 *   false  - Any failure (invalid args, partial read, I/O error, out-of-range).
 */
 
bool vblk_read_blocks (vblk_t *dev, uint64_t lba, uint32_t count, void *dst);

// Resolve a vblk name (e.g. "/dev/a1" or "/dev/a") into:
//  - base key/path to pass into diskio_*
//  - starting byte offset of the slice (0 for whole-disk)
//  - total byte length of the slice (0 if unknown)
bool vblk_resolve_to_base(const char *name,
                          char *key_out, size_t key_sz,
                          uint64_t *base_off_bytes,
                          uint64_t *length_bytes);
						  
typedef struct vblk vblk_t;
vblk_t *vblk_open(const char *dev);
vblk_t *vblk_open_partition(const char *dev, int part_index);
void     vblk_close(vblk_t *blk);

int  blkdev_open(const char *spec, vblk_t **out);
void blkdev_close(vblk_t *dev);