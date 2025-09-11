// include/vfs.h
#pragma once
#include <stddef.h>   // size_t
#include <sys/types.h> // off_t (on Windows with MinGW this is fine with _FILE_OFFSET_BITS=64)
#include <stdint.h>
#include <stdbool.h>

// If you need vblk_t in this header for mount APIs:
#include "vblk.h"   // or: typedef struct vblk vblk_t;


/* Open flags (mirror POSIX-ish, adjust to your impl) */
enum {
    VFS_O_RDONLY = 0x0001,
    VFS_O_WRONLY = 0x0002,
    VFS_O_RDWR   = 0x0003,
    VFS_O_CREAT  = 0x0100,
    VFS_O_TRUNC  = 0x0200,
};

/* Core API */
int   vfs_open (const char *path, int flags);
int   vfs_close(int fd);
int   vfs_read (int fd, void *buf, size_t nbytes);
int   vfs_write(int fd, const void *buf, size_t nbytes);
off_t vfs_lseek(int fd, off_t offset, int whence);
int   vfs_mkdir(const char *path);  // return nonzero on success (as you used)

// Mountpoint/query helpers you already use elsewhere
const char *vfs_iso_mountpoint(void);
int        vfs_mkdir(const char *path);  // already present
bool       vfs_mount_iso_at(const char *path, vblk_t *dev);
bool       vfs_is_iso_path(const char *path);
bool       vfs_read_all(const char *path, uint8_t *out, uint32_t cap, uint32_t *out_len);
bool       vfs_write_ext2_root(const char *dst_path, const uint8_t *data, uint32_t len);
