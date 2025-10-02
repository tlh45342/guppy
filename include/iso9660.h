// include/iso9660.h

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifndef ISO_SECTOR_SIZE
#define ISO_SECTOR_SIZE 2048u
#endif

/* Forward-declare your block device type.
   If you already have vblk.h, include it instead and remove this forward decl. */
typedef struct vblk vblk_t;

/* Debug flags (optional). */
enum {
    ISO_DBG_NONE   = 0,
    ISO_DBG_MOUNT  = 1u << 0,
    ISO_DBG_DIR    = 1u << 1,
    ISO_DBG_READ   = 1u << 2,
    ISO_DBG_JOLIET = 1u << 3,
    ISO_DBG_ALL    = 0xFFFFFFFFu
};
extern uint32_t iso_dbg;  // ok if you also use your own dbg system elsewhere

/* Directory entry callback — FINAL 3-arg shape */
typedef void (*iso_dirent_cb)(
    const char *name,   /* decoded name (UTF-8; Joliet if available) */
    int         is_dir, /* nonzero if a directory */
    void       *user    /* user context */
);

/* Lightweight mount state */
typedef struct iso9660 {
    vblk_t   *dev;        /* backing device (2048-byte sectors assumed) */
    uint32_t  block_size; /* logical block size (usually 2048) */
    int       use_joliet; /* 1 if Joliet SVD selected */
    uint32_t  pvd_lba;    /* primary volume descriptor LBA (usually 16) */
    uint32_t  root_lba;   /* root directory extent LBA */
    uint32_t  root_size;  /* root directory extent size in bytes */
} iso9660_t;

typedef struct {
    vblk_t     *dev;
    uint32_t    extent_lba;     // start LBA of this dir's data
    uint32_t    data_len;       // directory byte length
    uint32_t    pos;            // current offset within dir (0..data_len)
    uint32_t    blksz;          // logical block size (e.g., 2048)
    uint8_t     sec[4096];      // scratch >= max blksz
    uint32_t    sec_lba;        // LBA of buffered sector, or UINT32_MAX if none
} iso_dir_it;


/* API */
bool iso_mount(vblk_t *dev, iso9660_t *out);

/**
 * Read exactly one ISO9660 logical sector (2048 bytes) from an ISO image.
 *
 * Parameters:
 *   iso  - (INPUT, const) Pointer to a mounted ISO9660 context. Must be non-NULL and
 *          must contain a valid backing virtual block device handle (iso->dev).
 *          This function does not modify *iso (hence the const).
 *
 *   lba  - (INPUT) ISO9660 Logical Block Address (LBA) in **2048-byte units**,
 *          counted from the start of the ISO image. For reference, the Primary
 *          Volume Descriptor (PVD) is at LBA 16 in a compliant image.
 *          This is **not** a byte offset and **not** the vblk’s 512-byte LBA.
 *
 *   dst  - (OUTPUT) Caller-provided buffer with space for **ISO_SECTOR_SIZE bytes**
 *          (typically 2048). On success, the function fills this buffer with the
 *          entire sector at the given ISO LBA. Must be non-NULL.
 *
 * Returns:
 *   true  - if one full 2048-byte ISO sector was read into 'dst'.
 *   false - on any failure (invalid args; I/O error from the underlying device;
 *           out-of-range request; partial read not permitted).
*/

bool iso_read_sector(const iso9660_t *iso, uint32_t lba, void *dst);

// ----------------------------------------------------------------------------------------

/* Resolve a directory by path like "/BOOT" or "/EFI/BOOT". */
bool iso_lookup_dir(iso9660_t *iso,
                    const char *path,
                    uint32_t *out_dir_lba,
                    uint32_t *out_dir_size);

/* Enumerate entries in a directory extent. */
bool iso_list_dir(iso9660_t *iso, uint32_t dir_lba, uint32_t dir_size,
                  iso_dirent_cb cb, void *user);

/* Read a file by absolute path into dst (up to max bytes).
   If out_read!=NULL, returns number of bytes copied. */
bool iso_read_file_by_path(iso9660_t *iso, const char *path,
                           void *dst, uint32_t max, uint32_t *out_read);

/* Resolve a path to its extent info (final component). */
bool iso_stat_path(iso9660_t *iso, const char *path,
                   uint32_t *out_lba, uint32_t *out_size, int *out_is_dir);
				   

// --------------------------------------------------------------------------
// Really in iso9660_walk.c but who's counting?

int  iso_walk_component(const iso9660_t *iso,
                        uint32_t dir_lba,
						uint32_t dir_size,
                        const char *want,
                        uint32_t *out_lba,
						uint32_t *out_size,
                        uint8_t  *out_flags,
						time_t *out_mtime);

struct file;      // from vfs.h

// Minimal directory payload carried in inode->i_private for ISO dirs.
// If you already have a payload type with these fields, you can reuse it.
typedef struct {
    uint32_t lba;    // start LBA of the directory extent (2048-byte sectors)
    uint32_t size;   // directory size in bytes
    uint8_t  flags;  // ISO file flags (optional; 0x02 = DIR)
} iso_dir_payload_t;

// getdents64 implementation for ISO9660 directories
// Returns: number of bytes written to 'buf', 0 on EOF, or -errno on error.
ssize_t iso_getdents64(struct file *dirf, void *buf, size_t cap);