// include/iso9660.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

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

/* API */
bool iso_mount(vblk_t *dev, iso9660_t *out);

/* Resolve a directory by path like "/BOOT" or "/EFI/BOOT". */
bool iso_lookup_dir(iso9660_t *iso, const char *path,
                    uint32_t *out_dir_lba, uint32_t *out_dir_size);

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
