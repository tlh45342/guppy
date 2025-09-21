#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "vblk.h"


/* ---- ssize_t portability ---------------------------------------------- */
/* POSIX toolchains get it from <sys/types.h>. MSVC uses SSIZE_T in <BaseTsd.h>. */
#if defined(_MSC_VER)
/* MSVC */
  #include <BaseTsd.h>
  #ifndef _SSIZE_T_DEFINED
    typedef SSIZE_T ssize_t;
    #define _SSIZE_T_DEFINED
  #endif
#else
/* GCC/Clang on Linux/macOS/MinGW */
  #include <sys/types.h>
#endif
/* ----------------------------------------------------------------------- */

/* ===== Portable VFS flags & modes (Linux-like, platform-independent) ===== */

/* Access mode (mask + values) */
#define VFS_O_ACCMODE   0x0003
#define VFS_O_RDONLY    0x0000
#define VFS_O_WRONLY    0x0001
#define VFS_O_RDWR      0x0002

/* Creation/behavior flags */
#define VFS_O_CREAT     0x0100  /* create if not exists */
#define VFS_O_EXCL      0x0200  /* with O_CREAT: fail if exists */
#define VFS_O_TRUNC     0x0400  /* truncate on open */
#define VFS_O_APPEND    0x0800  /* append writes */
#define VFS_O_DIRECTORY 0x1000  /* path must be a directory (router may enforce) */

/* Seek whence (for llseek-like calls) */
#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

/* Mode / type bits for g_stat.st_mode (POSIX-like) */
#define VFS_S_IFMT   0170000
#define VFS_S_IFREG  0100000
#define VFS_S_IFDIR  0040000
#define VFS_S_IFLNK  0120000

/* Type test helpers */
#define VFS_S_ISDIR(m) (((m) & VFS_S_IFMT) == VFS_S_IFDIR)
#define VFS_S_ISREG(m) (((m) & VFS_S_IFMT) == VFS_S_IFREG)
#define VFS_S_ISLNK(m) (((m) & VFS_S_IFMT) == VFS_S_IFLNK)

/* ===== Permission bits (POSIX-like; portable across Guppy) ===== */
#define VFS_S_IRUSR  00400
#define VFS_S_IWUSR  00200
#define VFS_S_IXUSR  00100
#define VFS_S_IRGRP  00040
#define VFS_S_IWGRP  00020
#define VFS_S_IXGRP  00010
#define VFS_S_IROTH  00004
#define VFS_S_IWOTH  00002
#define VFS_S_IXOTH  00001

/* Convenience composites */
#define VFS_S_IRWXU  (VFS_S_IRUSR|VFS_S_IWUSR|VFS_S_IXUSR)
#define VFS_S_IRWXG  (VFS_S_IRGRP|VFS_S_IWGRP|VFS_S_IXGRP)
#define VFS_S_IRWXO  (VFS_S_IROTH|VFS_S_IWOTH|VFS_S_IXOTH)

/* Common default modes */
#define VFS_MODE_FILE_0644 (VFS_S_IRUSR|VFS_S_IWUSR|VFS_S_IRGRP|VFS_S_IROTH)
#define VFS_MODE_DIR_0755  (VFS_S_IRWXU|VFS_S_IRGRP|VFS_S_IXGRP|VFS_S_IROTH|VFS_S_IXOTH)

/* Directory entry d_type codes (match Linux DT_* values for familiarity) */
typedef enum {
    VFS_DT_UNKNOWN = 0,
    VFS_DT_FIFO    = 1,
    VFS_DT_CHR     = 2,
    VFS_DT_DIR     = 4,
    VFS_DT_BLK     = 6,
    VFS_DT_REG     = 8,
    VFS_DT_LNK     = 10,
    VFS_DT_SOCK    = 12
} vfs_d_type_t;

/* Packed directory record (linux_dirent64-like, variable-length) */
typedef struct vfs_dirent64 {
    uint64_t d_ino;       /* inode number (0 if unknown) */
    int64_t  d_off;       /* next offset within directory stream */
    uint16_t d_reclen;    /* size of this record, including name+NUL */
    uint8_t  d_type;      /* VFS_DT_* (0 if unknown) */
    char     d_name[];    /* NUL-terminated name */
} vfs_dirent64_t;

/* ===== Forward decls for core types we reference ===== */
struct superblock; struct inode; struct file;
struct filesystem_type;

struct g_stat;      /* defined in vfs_stat.h */
struct g_statvfs;   /* defined in vfs_stat.h */


#ifndef VFS_MAX_MOUNTS
#define VFS_MAX_MOUNTS 32
#endif

typedef struct {
    char src[64];      // "/dev/a1"
    char fstype[16];   // "ext2"
    char target[128];  // "/mnt/a"
    char opts[64];     // "rw", "ro,noexec", etc.
} vfs_mount_t;

// ----------------------------------

void vfs_list_mounts(void);  // prints current mounts
int  vfs_register_mount(const char *src, const char *fstype,
                        const char *target, const char *opts);
int  vfs_unrecord_mount_by_target(const char *target);  // for umount later
int  vfs_mount_count(void);
const vfs_mount_t *vfs_mount_get(int index);

/* ---------------- VFS ops tables (driver method tables) ---------------- */

typedef struct super_ops {
    int  (*statfs)(struct superblock*, struct g_statvfs*); /* fill fs stats */
    int  (*syncfs)(struct superblock*);
    void (*kill_sb)(struct superblock*);                   /* unmount helper */
} super_ops_t;

typedef struct inode_ops {
    int   (*lookup)(struct inode* dir, const char *name, struct inode **out);
    int   (*mkdir)(struct inode* dir, const char *name, uint32_t mode);
    int   (*rmdir)(struct inode* dir, const char *name);
    int   (*unlink)(struct inode* dir, const char *name);
    int   (*rename)(struct inode* odir, const char *on,
                    struct inode* ndir, const char *nn);
    int   (*getattr)(struct inode*, struct g_stat*);       /* fill file meta */
    int   (*setattr)(struct inode*, const void *iattr_like);
    int   (*truncate)(struct inode*, uint64_t size);
    int   (*symlink)(struct inode* dir, const char *name, const char *target);
    int   (*readlink)(struct inode*, char *buf, size_t bufsz);
} inode_ops_t;

typedef struct file_ops {
    int     (*open)(struct inode*, struct file** out, int flags, uint32_t mode);
    int     (*release)(struct file*);
    ssize_t (*read)(struct file*, void *buf, size_t, uint64_t *pos);
    ssize_t (*write)(struct file*, const void *buf, size_t, uint64_t *pos);
    int     (*fsync)(struct file*);
    int     (*ioctl)(struct file*, unsigned long, void*);
    int     (*llseek)(struct file*, int64_t off, int whence, uint64_t *newpos);

    /* Directory iteration: return a batch of dirent64 records (pull model) */
    ssize_t (*getdents64)(struct file *dirf, void *buf, size_t bytes);
} file_ops_t;

/* ---------------- Core objects ---------------- */

typedef struct superblock {
    struct filesystem_type *fs_type;
    vblk_t   *bdev;
    uint32_t  block_size;
    struct inode *root;
    const super_ops_t *s_op;
    void     *fs_private;   /* driver state */
} superblock_t;

typedef struct inode {
    uint64_t        i_ino;
    uint32_t        i_mode;     /* type + perms (use VFS_S_*) */
    uint32_t        i_uid, i_gid;
    uint64_t        i_size;
    uint64_t        i_mtime, i_ctime, i_atime;
    uint32_t        i_nlink;
    superblock_t   *i_sb;
    const inode_ops_t *i_op;
    const file_ops_t  *i_fop;
    void *i_private;            /* FS-specific (e.g., FAT cluster chain) */
} inode_t;

typedef struct file {
    inode_t            *f_inode;
    uint64_t            f_pos;
    int                 f_flags;
    const file_ops_t   *f_op;         /* usually inode->i_fop */
    void               *private_data; /* FS-specific iterator, etc. */
} file_t;

// --------------------------------------------------------------------

typedef int (*vfs_fs_iter_cb)(const struct filesystem_type *fs, void *user);
int vfs_for_each_fs(vfs_fs_iter_cb cb, void *user);

/* ---------------- Filesystem driver descriptor ---------------- */

typedef struct filesystem_type {
    const char *name;   /* "fat", "vfat", "ext2", "iso9660", ... */

    /* Lightweight signature sniff (optional) */
    bool (*probe)(vblk_t *dev, char *label_out, size_t label_cap);

    /* Mount constructs a superblock (+ root inode inside it) */
    int  (*mount)(vblk_t *dev, const char *opts, superblock_t **out_sb);

    /* Unmount helper; typically forwards to sb->s_op->kill_sb */
    void (*umount)(superblock_t *sb);
} filesystem_type_t;

/* ---------------- VFS router (path-based) ---------------- */

int     vfs_register(const filesystem_type_t *fst);
int     vfs_register_alias(const char *alias, const filesystem_type_t *target); /* optional */
const filesystem_type_t* vfs_find_fs(const char *name);                       /* optional */

/* ----------------- mount related ---------------------- */

int  vfs_mount_dev(const char *fstype,
                   const char *src,        // e.g. "/dev/a1"
                   vblk_t *dev,
                   const char *mountpoint,
                   const char *opts);
int     vfs_umount(const char *mountpoint);
void    vfs_list_mounts(void);

/* File/path operations */
int     vfs_open(const char *path, int flags, uint32_t mode, struct file **out);
int     vfs_close(struct file *f);
ssize_t vfs_read(struct file *f, void *buf, size_t n);
ssize_t vfs_write(struct file *f, const void *buf, size_t n);
int vfs_mkdir(const char *path, unsigned mode);   /* create one directory level */

/* Metadata */
int     vfs_stat(const char *path, struct g_stat *st);
int     vfs_statfs(const char *path, struct g_statvfs *svfs);

