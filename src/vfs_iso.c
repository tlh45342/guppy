// src/vfs_iso.c — ISO9660 (read-only) driver for Guppy VFS
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>   // offsetof
#include <stdio.h>    // snprintf

#include "vfs.h"
#include "vfs_stat.h"
#include "iso9660.h"

/* ===== Mount state ===== */
typedef struct {
    iso9660_t iso;   /* ISO state for this mount */
} iso_fs_t;

/* ===== Small utils ===== */
#ifndef VFS_PATH_MAX
#define VFS_PATH_MAX 1024
#endif

static char *dupstr(const char *s) {
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

/* ===== Inode/file private payloads ===== */
typedef struct {
    iso_fs_t *fs;
    char     *rel;      /* path relative to mount root ("" for root) */
    uint64_t  size;     /* for regular files */
    bool      is_dir;
} iso_inode_t;

typedef struct {
    iso_inode_t *node;
    uint8_t *buf; size_t len, cap;  /* lazy-loaded file content */
} iso_file_t;

/* ===== super_ops ===== */
static int iso_statfs(struct superblock *sb, struct g_statvfs *sv) {
    (void)sb;
    if (!sv) return -1;
    memset(sv, 0, sizeof *sv);
    sv->f_bsize = 2048;    /* common ISO block size */
    sv->f_frsize = 2048;
    sv->f_namemax = 255;
    sv->f_flag = G_VFS_FLAG_RDONLY;
    return 0;
}

static int iso_syncfs(struct superblock *sb) { (void)sb; return 0; }

static void iso_kill_sb(struct superblock *sb) {
    if (!sb) return;
    if (sb->root) {
        iso_inode_t *ip = (iso_inode_t*)sb->root->i_private;
        if (ip) { free(ip->rel); free(ip); }
        free(sb->root);
    }
    iso_fs_t *fs = (iso_fs_t*)sb->fs_private;
    free(fs);
    free(sb);
}

/* ===== file_ops: files (read-only) ===== */
static int iso_file_release(struct file *f) {
    if (!f) return 0;
    iso_file_t *fp = (iso_file_t*)f->private_data;
    if (fp) { free(fp->buf); free(fp); }
    free(f);
    return 0;
}

static ssize_t iso_file_read(struct file *f, void *buf, size_t n, uint64_t *pos) {
    iso_file_t *fp = (iso_file_t*)f->private_data;
    if (!fp || !fp->node || !buf) return -1;

    /* Lazy-load file content on first read */
    if (!fp->buf) {
        size_t need = (size_t)fp->node->size;
        if (need == 0) { return 0; }
        fp->buf = (uint8_t*)malloc(need ? need : 1);
        if (!fp->buf) return -1;

        uint32_t got = 0;
        if (!iso_read_file_by_path(&fp->node->fs->iso, fp->node->rel, fp->buf, (uint32_t)need, &got)
            || got != need) {
            free(fp->buf); fp->buf = NULL; fp->len = fp->cap = 0;
            return -1;
        }
        fp->len = need;
        fp->cap = need;
    }

    uint64_t p = pos ? *pos : f->f_pos;
    if (p >= fp->len) return 0;
    size_t avail = fp->len - (size_t)p;
    size_t take = (n < avail) ? n : avail;
    memcpy(buf, fp->buf + p, take);
    p += take;
    if (pos) *pos = p; else f->f_pos = p;
    return (ssize_t)take;
}

static int iso_file_fsync(struct file *f) { (void)f; return 0; }
static int iso_file_ioctl(struct file *f, unsigned long c, void *a) { (void)f;(void)c;(void)a; return -1; }
static int iso_file_llseek(struct file *f, int64_t off, int whence, uint64_t *newpos) {
    iso_file_t *fp = (iso_file_t*)f->private_data;
    uint64_t base;
    switch (whence) {
        case VFS_SEEK_SET: base = 0; break;
        case VFS_SEEK_CUR: base = f->f_pos; break;
        case VFS_SEEK_END: base = (fp && fp->node) ? fp->node->size : 0; break;
        default: return -1;
    }
    uint64_t np = base + off;
    f->f_pos = np;
    if (newpos) *newpos = np;
    return 0;
}

static int iso_file_open(struct inode *ino, struct file **out, int flags, uint32_t mode) {
    (void)mode;
    if (!out) return -1;
    *out = NULL;
    /* read-only FS: only allow read */
    if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY) return -1;

    iso_inode_t *ip = (iso_inode_t*)ino->i_private;
    if (!ip || ip->is_dir) return -1;

    struct file *f = (struct file*)calloc(1, sizeof *f);
    if (!f) return -1;
    iso_file_t *fp = (iso_file_t*)calloc(1, sizeof *fp);
    if (!fp) { free(f); return -1; }
    fp->node = ip;

    f->f_inode = ino;
    f->f_pos   = 0;
    f->f_flags = flags;
    f->private_data = fp;

    static const file_ops_t FOPS = {
        .open    = iso_file_open,  /* not used after first call */
        .release = iso_file_release,
        .read    = iso_file_read,
        .write   = NULL,           /* read-only */
        .fsync   = iso_file_fsync,
        .ioctl   = iso_file_ioctl,
        .llseek  = iso_file_llseek,
        .getdents64 = NULL,        /* files don't iterate */
    };
    f->f_op = &FOPS;

    *out = f;
    return 0;
}

/* ===== file_ops: directories (getdents64) ===== */

/* Collect directory entries into a simple vector so we can page by f_pos */
typedef struct {
    char   **names;
    uint8_t *types;  /* VFS_DT_DIR or VFS_DT_REG */
    size_t   count, cap;
} dirlist_t;

static void dirlist_free(dirlist_t *dl) {
    if (!dl) return;
    for (size_t i = 0; i < dl->count; ++i) free(dl->names[i]);
    free(dl->names); free(dl->types);
    dl->names = NULL; dl->types = NULL; dl->count = dl->cap = 0;
}

static bool dirlist_push(dirlist_t *dl, const char *name, int is_dir) {
    if (!dl || !name || name[0] == '\0') return true; /* skip empties */
    if ((name[0] == '.' && name[1] == '\0') ||
        (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return true; /* skip . and ..; ls can show them if you prefer */
    }
    if (dl->count == dl->cap) {
        size_t nc = dl->cap ? dl->cap * 2 : 32;
        char **nn = (char**)realloc(dl->names, nc * sizeof(char*));
        uint8_t *tt = (uint8_t*)realloc(dl->types, nc * sizeof(uint8_t));
        if (!nn || !tt) { free(nn); free(tt); return false; }
        dl->names = nn; dl->types = tt; dl->cap = nc;
    }
    char *copy = dupstr(name);
    if (!copy) return false;
    dl->names[dl->count] = copy;
    dl->types[dl->count] = is_dir ? VFS_DT_DIR : VFS_DT_REG;
    dl->count++;
    return true;
}

static void iso_collect_cb(const char *name, int is_dir, void *u) {
    dirlist_t *dl = (dirlist_t*)u;
    (void)dirlist_push(dl, name, is_dir);
}

static ssize_t iso_dir_getdents64(struct file *dirf, void *buf, size_t bytes) {
    if (!dirf || !buf || bytes < sizeof(vfs_dirent64_t)) return -1;

    iso_inode_t *ip = (iso_inode_t*)dirf->f_inode->i_private;
    if (!ip || !ip->is_dir) return -1;

    /* Look up directory extent and enumerate entries */
    uint32_t lba = 0, size = 0;
    if (!iso_lookup_dir(&ip->fs->iso, ip->rel, &lba, &size)) return -1;

    dirlist_t dl = {0};
    if (!iso_list_dir(&ip->fs->iso, lba, size, iso_collect_cb, &dl)) {
        dirlist_free(&dl);
        return -1;
    }

    size_t start = (size_t)dirf->f_pos;   /* number of entries already returned */
    if (start > dl.count) start = dl.count;

    uint8_t *out = (uint8_t*)buf;
    size_t written = 0;
    size_t emitted = 0;

    for (size_t i = start; i < dl.count; ++i) {
        const char *name = dl.names[i];
        size_t nlen = strlen(name);
        size_t reclen = offsetof(vfs_dirent64_t, d_name) + nlen + 1;
        if (written + reclen > bytes) break;

        vfs_dirent64_t *de = (vfs_dirent64_t*)(out + written);
        de->d_ino   = 0;                               /* unknown */
        de->d_off   = (int64_t)(i + 1);               /* best-effort */
        de->d_reclen= (uint16_t)reclen;
        de->d_type  = dl.types[i];
        memcpy(de->d_name, name, nlen);
        de->d_name[nlen] = '\0';

        written += reclen;
        emitted++;
    }

    dirf->f_pos += emitted;
    dirlist_free(&dl);
    return (ssize_t)written;
}

static int iso_dir_release(struct file *f) { free(f); return 0; }
static int iso_dir_open(struct inode *ino, struct file **out, int flags, uint32_t mode) {
    (void)mode;
    if (!out) return -1;
    *out = NULL;
    if (!(flags & VFS_O_DIRECTORY)) return -1;  /* require directory open */

    iso_inode_t *ip = (iso_inode_t*)ino->i_private;
    if (!ip || !ip->is_dir) return -1;

    struct file *f = (struct file*)calloc(1, sizeof *f);
    if (!f) return -1;

    static const file_ops_t FOPS_DIR = {
        .open    = iso_dir_open,
        .release = iso_dir_release,
        .read    = NULL,
        .write   = NULL,
        .fsync   = NULL,
        .ioctl   = NULL,
        .llseek  = NULL,
        .getdents64 = iso_dir_getdents64,
    };

    f->f_inode = ino;
    f->f_pos   = 0;
    f->f_flags = flags;
    f->f_op    = &FOPS_DIR;
    f->private_data = NULL;
    *out = f;
    return 0;
}

/* ===== inode_ops ===== */

static int iso_getattr(struct inode *ino, struct g_stat *st) {
    if (!ino || !st) return -1;
    memset(st, 0, sizeof *st);
    iso_inode_t *ip = (iso_inode_t*)ino->i_private;
    if (!ip) return -1;
    st->st_mode = ip->is_dir ? VFS_S_IFDIR : VFS_S_IFREG;
    st->st_size = ip->size;
    return 0;
}

static int iso_lookup(struct inode *dir, const char *name, struct inode **out) {
    if (out) *out = NULL;
    if (!dir || !name) return -1;
    iso_inode_t *dp = (iso_inode_t*)dir->i_private;
    if (!dp || !dp->fs || !dp->is_dir) return -1;

    char rel[VFS_PATH_MAX];
    join_relpath(dp->rel ? dp->rel : "", name, rel, sizeof rel);

    uint32_t lba = 0, size = 0; int is_dir = 0;
    if (!iso_stat_path(&dp->fs->iso, rel, &lba, &size, &is_dir))
        return 0; /* not found is not an error (lookup) */

    inode_t *ino = (inode_t*)calloc(1, sizeof *ino);
    if (!ino) return -1;

    iso_inode_t *ip = (iso_inode_t*)calloc(1, sizeof *ip);
    if (!ip) { free(ino); return -1; }
    ip->fs = dp->fs;
    ip->rel = dupstr(rel);
    if (!ip->rel) { free(ip); free(ino); return -1; }
    ip->is_dir = (is_dir != 0);
    ip->size   = size;

    ino->i_ino  = 0; /* unknown */
    ino->i_mode = ip->is_dir ? VFS_S_IFDIR : VFS_S_IFREG;
    ino->i_sb   = dir->i_sb;
    ino->i_op   = dir->i_op; /* same ops table */
    ino->i_private = ip;

    /* Choose file ops based on type (open routers) */
    if (ip->is_dir) {
        static const file_ops_t OPEN_DIR = {
            .open = iso_dir_open, .release=NULL,.read=NULL,.write=NULL,
            .fsync=NULL,.ioctl=NULL,.llseek=NULL,.getdents64=iso_dir_getdents64
        };
        ino->i_fop = &OPEN_DIR;
    } else {
        static const file_ops_t OPEN_FILE = {
            .open = iso_file_open, .release=iso_file_release, .read=iso_file_read,
            .write=NULL,.fsync=iso_file_fsync,.ioctl=iso_file_ioctl,.llseek=iso_file_llseek,
            .getdents64=NULL
        };
        ino->i_fop = &OPEN_FILE;
    }

    if (out) *out = ino;
    return 0;
}

static int iso_mkdir(struct inode *d, const char *n, uint32_t mode) { (void)d;(void)n;(void)mode; return -1; }
static int iso_rmdir(struct inode *d, const char *n) { (void)d;(void)n; return -1; }
static int iso_unlink(struct inode *d, const char *n){ (void)d;(void)n; return -1; }
static int iso_rename(struct inode *od, const char *on, struct inode *nd, const char *nn)
{ (void)od;(void)on;(void)nd;(void)nn; return -1; }
static int iso_setattr(struct inode *i, const void *a){ (void)i;(void)a; return -1; }
static int iso_truncate(struct inode *i, uint64_t s) { (void)i;(void)s; return -1; }
static int iso_symlink(struct inode *d, const char *n, const char *t)
{ (void)d;(void)n;(void)t; return -1; }
static int iso_readlink(struct inode *i, char *b, size_t z)
{ (void)i;(void)b;(void)z; return -1; }

static const inode_ops_t ISO_IOPS = {
    .lookup   = iso_lookup,
    .mkdir    = iso_mkdir,
    .rmdir    = iso_rmdir,
    .unlink   = iso_unlink,
    .rename   = iso_rename,
    .getattr  = iso_getattr,
    .setattr  = iso_setattr,
    .truncate = iso_truncate,
    .symlink  = iso_symlink,
    .readlink = iso_readlink,
};

/* ===== probe / mount / umount ===== */

/* Probe: try to parse the PVD. Optionally emit a label. */
static bool iso_probe(vblk_t *dev, char *label_out, size_t label_cap) {
    iso9660_t tmp;
    bool ok = iso_mount(dev, &tmp);
    if (ok && label_out && label_cap) {
        /* TODO: copy volume label if your iso9660 API exposes it */
        label_out[0] = '\0';
    }
    return ok;
}

static int iso_mount_fs(vblk_t *dev, const char *opts, superblock_t **out_sb) {
    (void)opts;
    if (!out_sb) return -1;
    *out_sb = NULL;

    iso_fs_t *fs = (iso_fs_t*)calloc(1, sizeof *fs);
    if (!fs) return -1;
    if (!iso_mount(dev, &fs->iso)) { free(fs); return -1; }

    superblock_t *sb = (superblock_t*)calloc(1, sizeof *sb);
    if (!sb) { free(fs); return -1; }

    static const super_ops_t SOP = {
        .statfs = iso_statfs,
        .syncfs = iso_syncfs,
        .kill_sb= iso_kill_sb,
    };

    inode_t *root = (inode_t*)calloc(1, sizeof *root);
    if (!root) { free(sb); free(fs); return -1; }

    iso_inode_t *rip = (iso_inode_t*)calloc(1, sizeof *rip);
    if (!rip) { free(root); free(sb); free(fs); return -1; }
    rip->fs = fs;
    rip->rel = dupstr("");
    if (!rip->rel) { free(rip); free(root); free(sb); free(fs); return -1; }
    rip->is_dir = true;
    rip->size = 0;

    root->i_ino  = 1;
    root->i_mode = VFS_S_IFDIR;
    root->i_sb   = sb;
    root->i_op   = &ISO_IOPS;

    /* Provide open tables that route to dir open handler */
    static const file_ops_t OPEN_DIR = {
        .open = iso_dir_open, .release=NULL,.read=NULL,.write=NULL,
        .fsync=NULL,.ioctl=NULL,.llseek=NULL,.getdents64=iso_dir_getdents64
    };
    root->i_fop  = &OPEN_DIR;
    root->i_private = rip;

    sb->fs_type    = NULL;
    sb->bdev       = dev;
    sb->block_size = 2048;
    sb->root       = root;
    sb->s_op       = &SOP;
    sb->fs_private = fs;

    *out_sb = sb;
    return 0;
}

static void iso_umount_fs(superblock_t *sb) {
    iso_kill_sb(sb);
}

/* Exported driver symbol */
const filesystem_type_t VFS_ISO9660 = {
    .name   = "iso9660",
    .probe  = iso_probe,
    .mount  = iso_mount_fs,
    .umount = iso_umount_fs,
};

/* Optional convenience wrapper (if you still want it) */
bool vfs_mount_iso_at(const char *mountpoint, vblk_t *dev) {
    /* Pass a non-NULL 'src' to match the 5-arg vfs_mount_dev signature. */
    return vfs_mount_dev("iso9660", "-", dev, mountpoint, "") == 0;
}
