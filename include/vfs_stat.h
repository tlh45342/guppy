// include/vfs_stat.h
#pragma once
#include <stdint.h>

/*
 * Portable VFS stat structs (Linux/POSIX-like, platform-independent).
 * - Use VFS_S_* mode/type bits from vfs.h for st_mode interpretation.
 * - All counters are 64-bit to avoid overflow on large volumes/files.
 */

typedef struct g_timespec {
    int64_t tv_sec;   /* seconds since epoch */
    int32_t tv_nsec;  /* nanoseconds [0..999,999,999] */
} g_timespec;

/* File metadata (analog of struct stat) */
struct g_stat {
    uint64_t  st_dev;      /* device id of filesystem (optional: mount-specific) */
    uint64_t  st_ino;      /* inode number within the filesystem */
    uint32_t  st_mode;     /* type + perms (use VFS_S_* and VFS_S_IS* helpers) */
    uint32_t  st_nlink;    /* link count */
    uint32_t  st_uid;      /* owner user id (0 if unsupported) */
    uint32_t  st_gid;      /* owner group id (0 if unsupported) */
    uint64_t  st_rdev;     /* device id (if special file), else 0 */
    uint64_t  st_size;     /* size in bytes (for regular files) */
    uint32_t  st_blksize;  /* "optimal" I/O block size (e.g., FS block size) */
    uint32_t  _pad_blksz;  /* reserved/padding to keep 8-byte alignment */
    uint64_t  st_blocks;   /* number of 512-byte blocks allocated (or 0 if N/A) */

    g_timespec st_atim;    /* last access time */
    g_timespec st_mtim;    /* last modification time (data) */
    g_timespec st_ctim;    /* last status change time (metadata) */
};

/* Filesystem statistics (analog of struct statvfs) */
struct g_statvfs {
    uint64_t f_bsize;    /* fundamental file system block size */
    uint64_t f_frsize;   /* fragment size (often == f_bsize) */

    uint64_t f_blocks;   /* total data blocks in filesystem */
    uint64_t f_bfree;    /* free blocks in filesystem */
    uint64_t f_bavail;   /* free blocks available to unprivileged user */

    uint64_t f_files;    /* total inodes (file nodes) */
    uint64_t f_ffree;    /* free inodes */
    uint64_t f_favail;    /* free inodes available to unprivileged user */

    uint64_t f_fsid;     /* filesystem ID (driver-defined) */
    uint64_t f_flag;     /* mount flags (driver/router-defined bitmask) */
    uint64_t f_namemax;  /* max filename length */
};

/* Optional: common f_flag bits (match your mount/router policy) */
enum {
    G_VFS_FLAG_RDONLY   = 1u << 0,  /* read-only mount */
    G_VFS_FLAG_NOEXEC   = 1u << 1,  /* execution not permitted */
    G_VFS_FLAG_NOSUID   = 1u << 2,  /* ignore setuid/setgid bits */
    G_VFS_FLAG_NODEV    = 1u << 3,  /* special files disallowed */
    G_VFS_FLAG_SYNCHRONOUS = 1u << 4 /* metadata/data sync on write */
};

/* Convenience: accessor names like POSIX st_atime/st_mtime if you prefer */
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec