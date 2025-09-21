#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"   // for struct file, VFS_O_DIRECTORY, vfs_open/close, vfs_d_type_t

/* Max filename (adjust if your FS supports more) */
#ifndef GU_DIRENT_NAME_MAX
#define GU_DIRENT_NAME_MAX 255
#endif

/* Opaque directory stream (like POSIX DIR*) */
typedef struct DIR DIR;

/* POSIX-like dirent (fixed-size name buffer for simplicity) */
struct dirent {
    uint64_t      d_ino;                 /* inode number if known, else 0 */
    unsigned char d_type;                /* VFS_DT_* or VFS_DT_UNKNOWN */
    char          d_name[GU_DIRENT_NAME_MAX + 1]; /* NUL-terminated */
};

/* API mirrors opendir/readdir/closedir (namespaced with vfs_) */
DIR*            vfs_opendir(const char *path);
struct dirent*  vfs_readdir(DIR *dir);   /* returns internal pointer; NULL=end/error */
int             vfs_closedir(DIR *dir);
void            vfs_rewinddir(DIR *dir); /* best-effort; uses llseek if available */