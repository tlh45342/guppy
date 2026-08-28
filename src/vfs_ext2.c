// src/vfs_ext2.c — minimal EXT2 shim for the Guppy VFS
// Supports: mount, mkdir, create+write (flush on close).
// Not yet: readdir, file reads, stat fidelity.

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "vblk.h"
#include "debug.h"
#include "vfs.h"
#include "vfs_stat.h"
#include "ext2.h"

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


/* -------- minimal on-disk EXT2 reader (single-group images for now) -------- */
static uint16_t rd16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

typedef struct ext2_disk_layout {
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t inode_table;
} ext2_disk_layout_t;

typedef struct ext2_disk_inode {
    uint16_t mode;
    uint32_t size;
    uint32_t atime, ctime, mtime;
    uint16_t links;
    uint32_t blocks512;
    uint32_t block[15];
} ext2_disk_inode_t;

static bool ext2_read_layout(vblk_t *dev, ext2_disk_layout_t *lo) {
    uint8_t sb[1024];
    uint8_t gd[32];
    if (!dev || !lo) return false;
    if (!vblk_read_bytes(dev, 1024, sizeof sb, sb)) return false;
    if (rd16le(sb + 56) != 0xEF53) return false;

    uint32_t log_bs = rd32le(sb + 24);
    if (log_bs > 2) return false; /* 1K, 2K, 4K only */
    lo->block_size = 1024u << log_bs;
    lo->inode_size = rd16le(sb + 88);
    if (lo->inode_size == 0) lo->inode_size = 128;
    if (lo->inode_size < 128 || lo->inode_size > lo->block_size) return false;

    uint32_t first_data_block = rd32le(sb + 20);
    uint64_t gd_off = (uint64_t)(first_data_block + 1u) * lo->block_size;
    if (!vblk_read_bytes(dev, gd_off, sizeof gd, gd)) return false;
    lo->inode_table = rd32le(gd + 8);
    return lo->inode_table != 0;
}

static bool ext2_read_inode_disk(vblk_t *dev, uint32_t ino,
                                 ext2_disk_inode_t *out) {
    ext2_disk_layout_t lo;
    uint8_t raw[256];
    if (!out || ino == 0 || !ext2_read_layout(dev, &lo)) return false;
    if (lo.inode_size > sizeof raw) return false;

    uint64_t off = (uint64_t)lo.inode_table * lo.block_size
                 + (uint64_t)(ino - 1u) * lo.inode_size;
    if (!vblk_read_bytes(dev, off, lo.inode_size, raw)) return false;

    memset(out, 0, sizeof *out);
    out->mode      = rd16le(raw + 0);
    out->size      = rd32le(raw + 4);
    out->atime     = rd32le(raw + 8);
    out->ctime     = rd32le(raw + 12);
    out->mtime     = rd32le(raw + 16);
    out->links     = rd16le(raw + 26);
    out->blocks512 = rd32le(raw + 28);
    for (int i = 0; i < 15; ++i) out->block[i] = rd32le(raw + 40 + i * 4);
    return out->mode != 0;
}

static uint8_t ext2_ft_to_vfs(uint8_t t) {
    switch (t) {
        case 1: return VFS_DT_REG;
        case 2: return VFS_DT_DIR;
        case 7: return VFS_DT_LNK;
        default: return VFS_DT_UNKNOWN;
    }
}

static bool ext2_find_dir_entry(vblk_t *dev, uint32_t dir_ino, const char *name,
                                uint32_t *ino_out, uint8_t *type_out) {
    ext2_disk_layout_t lo;
    ext2_disk_inode_t dir;
    if (!dev || !name || dir_ino == 0 || !ext2_read_layout(dev, &lo) ||
        !ext2_read_inode_disk(dev, dir_ino, &dir)) return false;

    uint8_t *blk = (uint8_t*)malloc(lo.block_size);
    if (!blk) return false;
    bool found = false;

    for (int bi = 0; bi < 12 && dir.block[bi] && !found; ++bi) {
        if (!vblk_read_bytes(dev, (uint64_t)dir.block[bi] * lo.block_size,
                             lo.block_size, blk)) break;
        uint32_t off = 0;
        while (off + 8 <= lo.block_size) {
            uint32_t ino = rd32le(blk + off);
            uint16_t rec = rd16le(blk + off + 4);
            uint8_t nl = blk[off + 6];
            uint8_t ft = blk[off + 7];
            if (rec < 8 || off + rec > lo.block_size) break;
            if (ino && nl && nl <= rec - 8 && strlen(name) == nl &&
                memcmp(blk + off + 8, name, nl) == 0) {
                if (ino_out) *ino_out = ino;
                if (type_out) *type_out = ft;
                found = true;
                break;
            }
            off += rec;
        }
    }
    free(blk);
    return found;
}

/* -------- mount state -------- */
typedef struct ext2_fs {
    vblk_t *dev;
} ext2_fs_t;

/* -------- inode/file priv payloads -------- */
typedef struct ext2_inode_priv {
    ext2_fs_t *fs;
    char      *rel;     /* relative path from mount root; "" for root */
    bool       is_dir;
} ext2_inode_priv_t;

typedef struct ext2_file_priv {
    ext2_inode_priv_t *node;
    uint8_t *buf;
    size_t len, cap;
    bool writable;
    bool dirty;
} ext2_file_priv_t;

/* -------- super_ops -------- */
static int s_statfs(struct superblock *sb, struct g_statvfs *sv) {
    if (!sv) return -1;
    memset(sv, 0, sizeof *sv);
    sv->f_bsize  = (sb && sb->block_size) ? sb->block_size : 1024;
    sv->f_frsize = sv->f_bsize;
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
    free(fs);
    free(sb);
}

/* -------- forward decl for i_open so we can reference it in file_ops -------- */
static int i_open(struct inode *ino, struct file **out, int flags, uint32_t mode);

/* -------- file_ops (write path; no read yet) -------- */
static int f_release(struct file *f) {
    int rc = 0;
    if (!f) return 0;

    ext2_file_priv_t *fp = (ext2_file_priv_t*)f->private_data;
    if (fp) {
        if (fp->writable && fp->dirty &&
            fp->node && fp->node->rel && fp->node->fs && fp->node->fs->dev) {
            char key[512];
            uint64_t off = 0, len = 0;

            if (!vblk_resolve_to_base(fp->node->fs->dev->name,
                                      key, sizeof key, &off, &len)) {
                DBG("ext2: cannot resolve backing store for '%s'",
                    fp->node->fs->dev->name);
                rc = -1;
            } else {
                int wrc;
                if (f->f_inode && f->f_inode->i_ino != 0) {
                    wrc = ext2_write_existing_at(key, off,
                                                 fp->node->rel,
                                                 fp->buf ? (const void*)fp->buf : (const void*)"",
                                                 fp->len);
                } else {
                    wrc = ext2_create_and_write(key, off,
                                                fp->node->rel,
                                                fp->buf ? (const void*)fp->buf : (const void*)"",
                                                fp->len);
                }
                DBG("ext2: flush '%s' key='%s' off=%llu len=%llu rc=%d",
                    fp->node->rel, key,
                    (unsigned long long)off,
                    (unsigned long long)fp->len, wrc);
                if (wrc != 0) rc = -1;
            }
        }
        free(fp->buf);
        free(fp);
    }
    free(f);
    return rc;
}
static ssize_t f_write(struct file *f, const void *buf, size_t n, uint64_t *pos) {
    ext2_file_priv_t *fp = (ext2_file_priv_t*)f->private_data;
    if (!fp || !fp->writable || !buf) return -1;
    if (fp->len + n > fp->cap) {
        size_t nc = fp->cap ? fp->cap * 2 : 4096;
        while (nc < fp->len + n) nc *= 2;
        uint8_t *nb = (uint8_t*)realloc(fp->buf, nc);
        if (!nb) return -1;
        fp->buf = nb; fp->cap = nc;
    }
    memcpy(fp->buf + fp->len, buf, n);
    fp->len += n;
    fp->dirty = true;
    if (pos) *pos += n;
    return (ssize_t)n;
}

static bool ext2_file_data_block(vblk_t *dev,
                                 const ext2_disk_layout_t *lo,
                                 const ext2_disk_inode_t *di,
                                 uint32_t logical_block,
                                 uint32_t *disk_block_out)
{
    if (!dev || !lo || !di || !disk_block_out) return false;
    const uint32_t ptrs = lo->block_size / 4u;
    uint8_t raw[4096];
    if (lo->block_size > sizeof raw) return false;

    if (logical_block < 12u) {
        *disk_block_out = di->block[logical_block];
        return true;
    }

    logical_block -= 12u;
    if (logical_block < ptrs) {
        if (di->block[12] == 0) { *disk_block_out = 0; return true; }
        if (!vblk_read_bytes(dev, (uint64_t)di->block[12] * lo->block_size,
                             lo->block_size, raw)) return false;
        *disk_block_out = rd32le(raw + logical_block * 4u);
        return true;
    }

    logical_block -= ptrs;
    if ((uint64_t)logical_block < (uint64_t)ptrs * ptrs) {
        uint32_t outer = logical_block / ptrs;
        uint32_t inner = logical_block % ptrs;
        if (di->block[13] == 0) { *disk_block_out = 0; return true; }
        if (!vblk_read_bytes(dev, (uint64_t)di->block[13] * lo->block_size,
                             lo->block_size, raw)) return false;
        uint32_t leaf = rd32le(raw + outer * 4u);
        if (leaf == 0) { *disk_block_out = 0; return true; }
        if (!vblk_read_bytes(dev, (uint64_t)leaf * lo->block_size,
                             lo->block_size, raw)) return false;
        *disk_block_out = rd32le(raw + inner * 4u);
        return true;
    }
    return false;
}

static ssize_t f_read(struct file *f, void *buf, size_t n, uint64_t *pos) {
    if (!f || !buf || !pos) return -1;

    ext2_file_priv_t *fp = (ext2_file_priv_t*)f->private_data;
    if (!fp || fp->writable || !fp->node || !fp->node->fs ||
        !fp->node->fs->dev || !f->f_inode || f->f_inode->i_ino == 0) {
        return -1;
    }

    ext2_disk_layout_t lo;
    ext2_disk_inode_t di;
    if (!ext2_read_layout(fp->node->fs->dev, &lo) ||
        !ext2_read_inode_disk(fp->node->fs->dev, f->f_inode->i_ino, &di)) {
        return -1;
    }

    if (*pos >= di.size || n == 0) return 0;

    uint64_t remain = (uint64_t)di.size - *pos;
    if ((uint64_t)n > remain) n = (size_t)remain;

    size_t done = 0;
    uint8_t *dst = (uint8_t*)buf;

    while (done < n) {
        uint64_t file_off = *pos + done;
        uint32_t logical_block = (uint32_t)(file_off / lo.block_size);
        uint32_t in_block = (uint32_t)(file_off % lo.block_size);

        size_t chunk = lo.block_size - in_block;
        if (chunk > n - done) chunk = n - done;

        uint32_t disk_block = 0;
        if (!ext2_file_data_block(fp->node->fs->dev, &lo, &di,
                                  logical_block, &disk_block)) {
            return done ? (ssize_t)done : -1;
        }
        if (disk_block == 0) {
            memset(dst + done, 0, chunk);
        } else {
            uint64_t disk_off = (uint64_t)disk_block * lo.block_size + in_block;
            if (!vblk_read_bytes(fp->node->fs->dev, disk_off, chunk, dst + done)) {
                return done ? (ssize_t)done : -1;
            }
        }

        done += chunk;
    }

    *pos += done;
    return (ssize_t)done;
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


static int d_open(struct inode *ino, struct file **out, int flags, uint32_t mode) {
    (void)mode;
    if (!ino || !out) return -1;
    *out = NULL;
    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)ino->i_private;
    if (!ip || !ip->is_dir) return -1;
    if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY) return -1;

    file_t *f = (file_t*)calloc(1, sizeof *f);
    if (!f) return -1;
    f->f_inode = ino;
    f->f_pos = 0;
    f->f_flags = flags;
    f->f_op = ino->i_fop;
    f->private_data = ip;
    *out = f;
    return 0;
}

static int d_release(struct file *f) {
    free(f);
    return 0;
}

static ssize_t d_getdents64(struct file *f, void *buf, size_t bytes) {
    if (!f || !buf || bytes == 0) return -1;
    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)f->private_data;
    if (!ip || !ip->is_dir || !ip->fs || !ip->fs->dev) return -1;

    ext2_disk_layout_t lo;
    ext2_disk_inode_t dirino;
    if (!ext2_read_layout(ip->fs->dev, &lo) ||
        !ext2_read_inode_disk(ip->fs->dev, f->f_inode->i_ino, &dirino)) return -1;

    uint8_t *blk = (uint8_t*)malloc(lo.block_size);
    if (!blk) return -1;

    size_t written = 0;
    uint64_t logical = 0;

    for (int bi = 0; bi < 12 && dirino.block[bi]; ++bi) {
        if (!vblk_read_bytes(ip->fs->dev,
                             (uint64_t)dirino.block[bi] * lo.block_size,
                             lo.block_size, blk)) {
            free(blk);
            return -1;
        }

        uint32_t off = 0;
        while (off + 8 <= lo.block_size && logical < dirino.size) {
            uint32_t ino = rd32le(blk + off);
            uint16_t rec = rd16le(blk + off + 4);
            uint8_t nl = blk[off + 6];
            uint8_t ft = blk[off + 7];
            if (rec < 8 || off + rec > lo.block_size) break;

            uint64_t next = logical + rec;
            if (next <= f->f_pos) {
                off += rec;
                logical = next;
                continue;
            }

            if (ino && nl && nl <= rec - 8) {
                size_t reclen = offsetof(vfs_dirent64_t, d_name) + (size_t)nl + 1u;
                reclen = (reclen + 7u) & ~7u;
                if (written + reclen > bytes) {
                    free(blk);
                    return (ssize_t)written;
                }

                vfs_dirent64_t *de = (vfs_dirent64_t*)((uint8_t*)buf + written);
                memset(de, 0, reclen);
                de->d_ino = ino;
                de->d_off = (int64_t)next;
                de->d_reclen = (uint16_t)reclen;
                de->d_type = ext2_ft_to_vfs(ft);
                memcpy(de->d_name, blk + off + 8, nl);
                de->d_name[nl] = '\0';
                written += reclen;
            }

            f->f_pos = next;
            off += rec;
            logical = next;
        }
    }

    free(blk);
    return (ssize_t)written;
}

static const file_ops_t EXT2_FOPS_DIR = {
    .open       = d_open,
    .release    = d_release,
    .read       = NULL,
    .write      = NULL,
    .fsync      = NULL,
    .ioctl      = NULL,
    .llseek     = NULL,
    .getdents64 = d_getdents64,
};

/* -------- inode_ops -------- */
static int i_getattr(struct inode *ino, struct g_stat *st) {
    if (!ino || !st) return -1;
    memset(st, 0, sizeof *st);
    st->st_ino = ino->i_ino;
    st->st_mode = ino->i_mode;
    st->st_nlink = ino->i_nlink;
    st->st_uid = ino->i_uid;
    st->st_gid = ino->i_gid;
    st->st_size = ino->i_size;
    st->st_blksize = ino->i_sb ? ino->i_sb->block_size : 1024;
    st->st_blocks = (ino->i_size + 511u) / 512u;
    st->st_atime = (int64_t)ino->i_atime;
    st->st_mtime = (int64_t)ino->i_mtime;
    st->st_ctime = (int64_t)ino->i_ctime;
    return 0;
}

static int i_mkdir(struct inode *dir, const char *name, uint32_t mode) {
    (void)mode; /* ext2 helpers ignore mode */
    if (!dir || !name) return -1;
    ext2_inode_priv_t *dp = (ext2_inode_priv_t*)dir->i_private;
    if (!dp || !dp->fs) return -1;

    char full[VFS_PATH_MAX];
    join_relpath(dp->rel ? dp->rel : "", name, full, sizeof full);

    char key[512];
    uint64_t off = 0, len = 0;
    if (!dp->fs->dev ||
        !vblk_resolve_to_base(dp->fs->dev->name, key, sizeof key, &off, &len)) {
        return -1;
    }
    if (ext2_mkdir_at(key, off, full, (uint16_t)(mode & 0777u)) != 0) return -1;
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

    /* Persistent lookup works for any on-disk directory inode. */
    uint32_t ino_num = 0;
    uint8_t ft = 0;
    if (dir->i_ino != 0 && ext2_find_dir_entry(dp->fs->dev, (uint32_t)dir->i_ino,
                                               name, &ino_num, &ft)) {
        ext2_disk_inode_t di;
        if (!ext2_read_inode_disk(dp->fs->dev, ino_num, &di)) return -1;

        inode_t *ino = (inode_t*)calloc(1, sizeof *ino);
        ext2_inode_priv_t *ip = (ext2_inode_priv_t*)calloc(1, sizeof *ip);
        if (!ino || !ip) { free(ip); free(ino); return -1; }
        ip->fs = dp->fs;
        ip->is_dir = ((di.mode & VFS_S_IFMT) == VFS_S_IFDIR) || ft == 2;
        ip->rel = xstrdup(full);
        if (!ip->rel) { free(ip); free(ino); return -1; }

        ino->i_ino = ino_num;
        ino->i_mode = di.mode;
        ino->i_size = di.size;
        ino->i_atime = di.atime;
        ino->i_ctime = di.ctime;
        ino->i_mtime = di.mtime;
        ino->i_nlink = di.links;
        ino->i_sb = dir->i_sb;
        ino->i_op = dir->i_op;
        ino->i_fop = ip->is_dir ? &EXT2_FOPS_DIR : &EXT2_FOPS_FILE;
        ino->i_private = ip;
        if (out) *out = ino;
        return 0;
    }

    return 0; /* not found */
}

static int i_readlink(struct inode *ino, char *buf, size_t bufsz) { (void)ino;(void)buf;(void)bufsz; return -1; }
static int i_setattr(struct inode *ino, const void *attr) { (void)ino;(void)attr; return -1; }
static int i_truncate(struct inode *ino, uint64_t size) {
    if (!ino || size != 0) return -1;
    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)ino->i_private;
    if (!ip || ip->is_dir || !ip->fs || !ip->fs->dev || !ip->rel) return -1;

    char key[512];
    uint64_t off = 0, len = 0;
    if (!vblk_resolve_to_base(ip->fs->dev->name, key, sizeof key, &off, &len))
        return -1;
    (void)len;

    if (ext2_truncate_at(key, off, ip->rel, 0) != 0) return -1;
    ino->i_size = 0;
    ino->i_mtime = ino->i_ctime = (uint64_t)time(NULL);
    return 0;
}
static int i_unlink(struct inode *d, const char *n) {
    if (!d || !n || !*n) return -1;
    ext2_inode_priv_t *dp = (ext2_inode_priv_t*)d->i_private;
    if (!dp || !dp->is_dir || !dp->fs || !dp->fs->dev) return -1;

    char rel[VFS_PATH_MAX];
    join_relpath(dp->rel, n, rel, sizeof rel);

    char key[512];
    uint64_t off = 0, len = 0;
    if (!vblk_resolve_to_base(dp->fs->dev->name, key, sizeof key, &off, &len))
        return -1;
    (void)len;
    return ext2_unlink_at(key, off, rel);
}
static int i_rename(struct inode *od, const char *on, struct inode *nd, const char *nn) { (void)od;(void)on;(void)nd;(void)nn; return -1; }
static int i_symlink(struct inode *d, const char *n, const char *t) { (void)d;(void)n;(void)t; return -1; }

/* forward (above) */
static int i_open(struct inode *ino, struct file **out, int flags, uint32_t mode) {
    (void)mode;  /* permissions not enforced yet */
    if (!ino || !out) return -1;
    *out = NULL;

    ext2_inode_priv_t *ip = (ext2_inode_priv_t*)ino->i_private;
    if (!ip || ip->is_dir) return -1;

    int accmode = flags & VFS_O_ACCMODE;
    if (accmode != VFS_O_RDONLY &&
        accmode != VFS_O_WRONLY &&
        accmode != VFS_O_RDWR) {
        return -1;
    }

    struct file *f = (struct file*)calloc(1, sizeof *f);
    if (!f) return -1;

    ext2_file_priv_t *fp = (ext2_file_priv_t*)calloc(1, sizeof *fp);
    if (!fp) { free(f); return -1; }

    fp->node = ip;
    fp->buf = NULL;
    fp->len = fp->cap = 0;
    fp->writable = (accmode != VFS_O_RDONLY);
    fp->dirty = false;

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

    superblock_t *sb = (superblock_t*)calloc(1, sizeof *sb);
    if (!sb) { free(fs); return -1; }

    static const super_ops_t SOP = {
        .statfs  = s_statfs,
        .syncfs  = s_syncfs,
        .kill_sb = s_kill_sb,
    };

    inode_t *root = (inode_t*)calloc(1, sizeof *root);
    if (!root) { free(sb); free(fs); return -1; }

    ext2_inode_priv_t *rip = (ext2_inode_priv_t*)calloc(1, sizeof *rip);
    if (!rip) { free(root); free(sb); free(fs); return -1; }

    rip->fs = fs;
    rip->is_dir = true;
    rip->rel = xstrdup("");
    if (!rip->rel) { free(rip); free(root); free(sb); free(fs); return -1; }

    root->i_ino  = 2;                /* conventional ext2 root */
    root->i_mode = VFS_S_IFDIR | VFS_MODE_DIR_0755;
    root->i_sb   = sb;
    root->i_op   = &EXT2_IOPS;
    root->i_fop  = &EXT2_FOPS_DIR;
    root->i_private = rip;

    ext2_disk_inode_t rdi;
    if (ext2_read_inode_disk(dev, 2, &rdi)) {
        root->i_mode  = rdi.mode;
        root->i_size  = rdi.size;
        root->i_atime = rdi.atime;
        root->i_ctime = rdi.ctime;
        root->i_mtime = rdi.mtime;
        root->i_nlink = rdi.links;
    }

    sb->fs_type    = NULL;
    sb->bdev       = dev;
    {
        ext2_disk_layout_t lo;
        if (!ext2_read_layout(dev, &lo)) {
            free(rip->rel); free(rip); free(root); free(sb); free(fs);
            return -1;
        }
        sb->block_size = lo.block_size;
    }
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
