// src/vfs_ext2.c — minimal EXT2 shim for the Guppy VFS
// Supports: mount, mkdir, create+write (flush on close).
// Not yet: readdir, file reads, stat fidelity.

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "vblk.h"
#include "vfs.h"
#include "vfs_stat.h"
#include "ext2.h"   // ext2_mkdir_p(const char*), ext2_create_and_write(const char*, const uint8_t*, uint32_t)

/* -------- feature toggles -------- */
#ifndef VFS_HAVE_CREATE_OP
#define VFS_HAVE_CREATE_OP 0   /* set to 1 in vfs.h when you add inode_ops->create */
#endif

#ifndef VFS_PATH_MAX
#define VFS_PATH_MAX 1024
#endif

/* -------- small utils -------- */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void join_relpath(const char *parent, const char *name, char *out, size_t cap) {
    if (!parent || parent[0] == '\0') {
        snprintf(out, cap, "%s", name ? name : "");
    } else if (!name || name[0] == '\0') {
        snprintf(out, cap, "%s", parent);
    } else {
        size_t lp = strlen(parent);
        if (lp && parent[lp - 1] == '/') snprintf(out, cap, "%s%s", parent, name);
        else                             snprintf(out, cap, "%s/%s", parent, name);
    }
    out[cap - 1] = '\0';
}

/* -------- mount state -------- */
typedef struct ext2_fs {
    vblk_t *dev;        /* reserved for future; helpers are global right now */
    /* naive dir registry so lookup can find what we created via this driver */
    char  **dirs;
    size_t  ndirs, capdirs;
} ext2_fs_t;

static bool ext2_dirs_add(ext2_fs_t *fs, const char *rel) {
    if (!fs || !rel) return false;
    for (size_t i = 0; i < fs->ndirs; ++i)
        if (strcmp(fs->dirs[i], rel) == 0) return true;
    if (fs->ndirs == fs->capdirs) {
        size_t nc = fs->capdirs ? fs->capdirs * 2 : 16;
        char **nv = (char**)realloc(fs->dirs, nc * sizeof(char*));
        if (!nv) return false;
        fs->dirs = nv; fs->capdirs = nc;
    }
    fs->dirs[fs->ndirs] = xstrdup(rel);
    if (!fs->dirs[fs->ndirs]) return false;
    fs->ndirs++;
    return true;
}
static bool ext2_dirs_has(ext2_fs_t *fs, const char *rel) {
    if (!fs || !rel) return false;
    for (size_t i = 0; i < fs->ndirs; ++i)
        if (strcmp(fs->dirs[i], rel) == 0) return true;
    return false;
}
static void ext2_dirs_free(ext2_fs_t *fs) {
    if (!fs) return;
    for (size_t i = 0; i < fs->ndirs; ++i) free(fs->dirs[i]);
    free(fs->dirs);
    fs->dirs = NULL; fs->ndirs = fs->capdirs = 0;
}

/* -------- inode/file priv payloads -------- */
typedef struct ext2_inode_priv {
    ext2_fs_t *fs;
    char      *rel;     /* relative path from mount root; "" for root */
    bool       is_dir;
} ext2_inode_priv_t;

typedef struct ext2_file_priv {
    ext2_inode_priv_t *node;
    uint8_t *buf; size_t len, cap;
} ext2_file_priv_t;

/* -------- super_ops -------- */
static int s_statfs(struct superblock *sb, struct g_statvfs *sv) {
    (void)sb;
    if (!sv) return -1;
    memset(sv, 0, sizeof *sv);
    sv->f_bsize  = 4096;
    sv->f_frsize = 4096;
    sv->f_namemax = 255;
    return 0;
}
static int s_syncfs(struct superblock *sb) { (void)sb; return 0; }
static void s_kill_sb(struct superblock *sb) {
    if (!sb) return;
    if (sb->root) {
        ext2_inode_priv_t *ip = (ext2_inode_priv_t*)sb->root->i_private;
        if (ip) { free(ip->rel); free(ip); }
        free(sb->root);
    }
    ext2_fs_t *fs = (ext2_fs_t*)sb->fs_private;
    ext2_dirs_free(fs);
    free(fs);
    free(sb);
}

/* -------- forward decl for i_open so we can reference it in file_ops -------- */
static int i_open(struct inode *ino, struct file **out, int flags, uint32_t mode);

/* -------- file_ops (write path; no read yet) -------- */
static int f_release(struct file *f) {
    if (!f) return 0;
    ext2_file_priv_t *fp = (ext2_file_priv_t*)f->private_data;
    if (fp) {
        if (fp->node && fp->node->rel) {
            (void)ext2_create_and_write(fp->node->rel, fp->buf, (uint32_t)fp->len);
        }
        free(fp->buf);
        free(fp);
    }
    free(f);
    return 0;
}
static ssize_t f_write(struct file *f, const void *buf, size_t n, uint64_t *pos) {
    (void)pos; /* append-only buffer for now */
    ext2_file_priv_t *fp = (ext2_file_priv_t*)f->private_data;
    if (!fp || !buf) return -1;
    if (fp->len + n > fp->cap) {
        size_t nc = fp->cap ? fp->cap * 2 : 4096;
        while (nc < fp->len + n) nc *= 2;
        uint8_t *nb = (uint8_t*)realloc(fp->buf, nc);
        if (!nb) return -1;
        fp->buf = nb; fp->cap = nc;
    }
    memcpy(fp->buf + fp->len, buf, n);
    fp->len += n;
    return (ssize_t)n;
}
static ssize_t f_read(struct file *f, void *buf, size_t n, uint64_t *pos) {
    (void)f; (void)buf; (void)n; (void)pos;
    return -1; /* not implemented yet */
}
static int f_fsync(struct file *f) { (void)f; return 0; }
static int f_ioctl(struct file *f, unsigned long c, void *a) { (void)f;(void)c;(void)a; return -1; }
static int f_llseek(struct file *f, int64_t off, int whence, uint64_t *newpos) {
    (void)f; (void)off; (void)whence; if (newpos) *newpos = 0; return 0;
}

static const file_ops_t EXT2_FOPS_FILE = {
    .open    = i_open,     /* <- now wired, removes 'i_open unused' warning */
    .release = f_release,
    .read    = f_read,
    .write   = f_write,
    .fsync   = f_fsync,
    .ioctl   = f_ioctl,
    .llseek  = f_llseek,
    .getdents64 = NULL,    /* no readdir yet */
};

/* -------- inode_ops -------- */
static int i_getattr(struct inode *ino, struct g_stat *st) {
    if (!ino || !st) return -1;
    memset(st, 0, sizeof *st);
    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)ino->i_private;
    st->st_mode = (ip && ip->is_dir) ? VFS_S_IFDIR : VFS_S_IFREG;
    return 0;
}

static int i_mkdir(struct inode *dir, const char *name, uint32_t mode) {
    (void)mode; /* ext2 helpers ignore mode */
    if (!dir || !name) return -1;
    ext2_inode_priv_t *dp = (ext2_inode_priv_t*)dir->i_private;
    if (!dp || !dp->fs) return -1;

    char full[VFS_PATH_MAX];
    join_relpath(dp->rel ? dp->rel : "", name, full, sizeof full);

    if (!ext2_mkdir_p(full)) return -1;
    if (!ext2_dirs_add(dp->fs, full)) return -1;
    return 0;
}

#if VFS_HAVE_CREATE_OP
static int i_create(struct inode *dir, const char *name, uint32_t mode, struct inode **out) {
    (void)mode;  /* permissions not enforced by helpers */
    if (!out || !dir || !name) return -1;
    ext2_inode_priv_t *dp = (ext2_inode_priv_t*)dir->i_private;
    if (!dp || !dp->fs) return -1;

    char full[VFS_PATH_MAX];
    join_relpath(dp->rel ? dp->rel : "", name, full, sizeof full);

    inode_t *ino = (inode_t*)calloc(1, sizeof *ino);
    if (!ino) return -1;

    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)calloc(1, sizeof *ip);
    if (!ip) { free(ino); return -1; }
    ip->fs = dp->fs;
    ip->is_dir = false;
    ip->rel = xstrdup(full);
    if (!ip->rel) { free(ip); free(ino); return -1; }

    ino->i_ino = 0;
    ino->i_mode = VFS_S_IFREG;
    ino->i_sb   = dir->i_sb;
    ino->i_op   = dir->i_op;
    ino->i_fop  = &EXT2_FOPS_FILE;
    ino->i_private = ip;

    *out = ino;
    return 0;
}
#endif

static int i_lookup(struct inode *dir, const char *name, struct inode **out) {
    if (out) *out = NULL;
    if (!dir || !name) return -1;
    ext2_inode_priv_t *dp = (ext2_inode_priv_t*)dir->i_private;
    if (!dp || !dp->fs) return -1;

    char full[VFS_PATH_MAX];
    join_relpath(dp->rel ? dp->rel : "", name, full, sizeof full);

    if (!ext2_dirs_has(dp->fs, full)) {
        return 0; /* not found (ok for lookup) */
    }

    inode_t *ino = (inode_t*)calloc(1, sizeof *ino);
    if (!ino) return -1;

    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)calloc(1, sizeof *ip);
    if (!ip) { free(ino); return -1; }
    ip->fs = dp->fs;
    ip->is_dir = true;
    ip->rel = xstrdup(full);
    if (!ip->rel) { free(ip); free(ino); return -1; }

    ino->i_ino = 0;
    ino->i_mode = VFS_S_IFDIR;
    ino->i_sb   = dir->i_sb;
    ino->i_op   = dir->i_op;
    ino->i_fop  = NULL;             /* no dir open/readdir yet */
    ino->i_private = ip;

    if (out) *out = ino;
    return 0;
}

static int i_readlink(struct inode *ino, char *buf, size_t bufsz) { (void)ino;(void)buf;(void)bufsz; return -1; }
static int i_setattr(struct inode *ino, const void *attr) { (void)ino;(void)attr; return -1; }
static int i_truncate(struct inode *ino, uint64_t size) { (void)ino;(void)size; return 0; }
static int i_unlink(struct inode *d, const char *n) { (void)d;(void)n; return -1; }
static int i_rename(struct inode *od, const char *on, struct inode *nd, const char *nn) { (void)od;(void)on;(void)nd;(void)nn; return -1; }
static int i_symlink(struct inode *d, const char *n, const char *t) { (void)d;(void)n;(void)t; return -1; }

/* forward (above) */
static int i_open(struct inode *ino, struct file **out, int flags, uint32_t mode) {
    (void)mode;  /* not enforced yet */
    if (!out) return -1;
    *out = NULL;

    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)ino->i_private;
    if (!ip || ip->is_dir) return -1; /* only files are openable here */

    /* write or append only for now */
    if ((flags & VFS_O_ACCMODE) == VFS_O_RDONLY) return -1;

    struct file *f = (struct file*)calloc(1, sizeof *f);
    if (!f) return -1;

    ext2_file_priv_t *fp = (ext2_file_priv_t*)calloc(1, sizeof *fp);
    if (!fp) { free(f); return -1; }

    fp->node = ip;
    fp->buf  = NULL; fp->len = fp->cap = 0;

    f->f_inode = ino;
    f->f_pos   = 0;
    f->f_flags = flags;
    f->f_op    = &EXT2_FOPS_FILE;
    f->private_data = fp;

    *out = f;
    return 0;
}

static const inode_ops_t EXT2_IOPS = {
    .lookup   = i_lookup,
    .mkdir    = i_mkdir,
    .rmdir    = NULL,
    .unlink   = i_unlink,
    .rename   = i_rename,
    .getattr  = i_getattr,
    .setattr  = i_setattr,
    .truncate = i_truncate,
    .symlink  = i_symlink,
    .readlink = i_readlink,
#if VFS_HAVE_CREATE_OP
    .create   = i_create,
#endif
};

/* -------- probe / mount / umount -------- */

/* ext2 sb lives at byte 1024; magic 0xEF53 at +56 within that 1024 */
static bool ext2_probe(vblk_t *dev, char *label_out, size_t label_cap) {
    uint8_t sb[1024];
    if (!vblk_read_bytes(dev, 1024, sizeof sb, sb)) return false;
    uint16_t magic = (uint16_t)(sb[56] | (sb[57] << 8));
    bool ok = (magic == 0xEF53);
    if (ok && label_out && label_cap) label_out[0] = '\0'; /* optional label */
    return ok;
}

static int ext2_mount(vblk_t *dev, const char *opts, superblock_t **out_sb) {
    (void)opts;
    if (!out_sb) return -1;
    *out_sb = NULL;

    ext2_fs_t *fs = (ext2_fs_t*)calloc(1, sizeof *fs);
    if (!fs) return -1;
    fs->dev = dev;

    /* remember mount root "" so lookup can resolve relative children we create */
    if (!ext2_dirs_add(fs, "")) { free(fs); return -1; }

    superblock_t *sb = (superblock_t*)calloc(1, sizeof *sb);
    if (!sb) { ext2_dirs_free(fs); free(fs); return -1; }

    static const super_ops_t SOP = {
        .statfs  = s_statfs,
        .syncfs  = s_syncfs,
        .kill_sb = s_kill_sb,
    };

    inode_t *root = (inode_t*)calloc(1, sizeof *root);
    if (!root) { free(sb); ext2_dirs_free(fs); free(fs); return -1; }

    ext2_inode_priv_t *rip = (ext2_inode_priv_t*)calloc(1, sizeof *rip);
    if (!rip) { free(root); free(sb); ext2_dirs_free(fs); free(fs); return -1; }

    rip->fs = fs;
    rip->is_dir = true;
    rip->rel = xstrdup("");
    if (!rip->rel) { free(rip); free(root); free(sb); ext2_dirs_free(fs); free(fs); return -1; }

    root->i_ino  = 2;                /* conventional ext2 root */
    root->i_mode = VFS_S_IFDIR;
    root->i_sb   = sb;
    root->i_op   = &EXT2_IOPS;
    root->i_fop  = NULL;             /* no dir open/readdir yet */
    root->i_private = rip;

    sb->fs_type    = NULL;
    sb->bdev       = dev;
    sb->block_size = 1024;
    sb->root       = root;
    sb->s_op       = &SOP;
    sb->fs_private = fs;

    *out_sb = sb;
    return 0;
}

static void ext2_umount(superblock_t *sb) {
    if (!sb) return;
    s_kill_sb(sb);
}

/* Exported driver symbol */
const filesystem_type_t VFS_EXT2 = {
    .name  = "ext2",
    .probe = ext2_probe,
    .mount = ext2_mount,
    .umount= ext2_umount,
};
