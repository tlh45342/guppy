// src/vfs_iso.c — ISO9660 (read-only) driver glue for Guppy VFS
// C11 only. Primary ISO9660 names (uppercase + optional ;1 version).

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>   // offsetof
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#include "vfs.h"
#include "vfs_stat.h"
#include "iso9660.h"   // iso9660_t, iso_mount(), iso_read_sector(), iso_walk_component()
#include "debug.h"     // DBG()

extern const filesystem_type_t VFS_ISO9660;

/* ===== Mount state kept in superblock->fs_private ===== */
typedef struct {
    iso9660_t iso;   /* low-level ISO state (PVD/SVD/root extent etc.) */
} iso_fs_t;

/* ===== Inode private payload ===== */
typedef struct {
    iso_fs_t *fs;
    bool      is_dir;
    uint32_t  extent_lba;   // start LBA of this inode's data/dir extent
    uint32_t  extent_size;  // size in bytes of this extent
} iso_inode_t;

/* ===== helpers: ASCII name handling for Primary ISO9660 ===== */
static inline int to_upper_ascii(unsigned char c) {
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

static inline uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void trim_version_semicolon(char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == ';') { s[i] = '\0'; break; }
    }
}

static void iso_primary_ident_to_cstr(const uint8_t *id, uint8_t id_len,
                                      char *out, size_t out_cap)
{
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!id || id_len == 0) return;

    // ISO9660 primary identifiers are generally upper-case plus '.' and ';'
    size_t n = (id_len < (uint8_t)(out_cap - 1)) ? id_len : (out_cap - 1);
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = id[i];
        if (c == 0) break;
        if (c == '/') c = '_';
        out[i] = (char)to_upper_ascii(c);
    }
    out[n] = '\0';
}

/* ===== super_ops ===== */
static int iso_statfs(struct superblock *sb, struct g_statvfs *sv) {
    if (!sb || !sv) return -1;
    memset(sv, 0, sizeof *sv);
    sv->f_bsize   = sb->block_size ? sb->block_size : 2048;
    sv->f_frsize  = sv->f_bsize;
    sv->f_namemax = 255;
    return 0;
}

static int iso_syncfs(struct superblock *sb) { (void)sb; return 0; }

static void iso_kill_sb(struct superblock *sb) {
    if (!sb) return;
    iso_fs_t *fs = (iso_fs_t*)sb->fs_private;

    // free root inode payload first
    if (sb->root) {
        iso_inode_t *rip = (iso_inode_t*)sb->root->i_private;
        free(rip);
        free(sb->root);
    }
    free(fs);
    free(sb);
}

/* ===== file (regular) file_ops ===== */
static int iso_file_open(struct inode *inode, struct file **out, int flags, uint32_t mode) {
    (void)mode;
    if (!inode || !out) return -1;
    if (flags & VFS_O_DIRECTORY) return -1; // not a directory
    iso_inode_t *ip = (iso_inode_t*)inode->i_private;
    if (!ip || ip->is_dir) return -1;

    struct file *f = (struct file*)calloc(1, sizeof *f);
    if (!f) return -1;
    f->f_inode = inode;
    f->f_flags = flags;
    f->f_pos   = 0;
    *out = f;
    return 0;
}

static int iso_file_release(struct file *f) { free(f); return 0; }

/* Read helper: copy from extent using iso_read_sector() */
static ssize_t iso_file_read(struct file *f, void *buf, size_t n, uint64_t *ppos) {
    if (!f || !buf || !ppos) return -1;
    iso_inode_t *ip = (iso_inode_t*)f->f_inode->i_private;
    if (!ip || ip->is_dir) return -EISDIR;

    const uint32_t bs = f->f_inode->i_sb ? f->f_inode->i_sb->block_size : 2048;
    uint64_t pos = *ppos;
    if (pos >= ip->extent_size) return 0; // EOF
    if (n > (size_t)(ip->extent_size - pos)) n = (size_t)(ip->extent_size - pos);

    uint8_t *dst = (uint8_t*)buf;
    size_t   copied = 0;

    uint32_t lba = ip->extent_lba + (uint32_t)(pos / bs);
    uint32_t in_sector = (uint32_t)(pos % bs);

    while (copied < n) {
        uint8_t sec[2048];
        if (!iso_read_sector(&ip->fs->iso, lba, sec)) return (copied > 0) ? (ssize_t)copied : -EIO;

        size_t take = bs - in_sector;
        size_t left = n - copied;
        if (take > left) take = left;

        memcpy(dst + copied, sec + in_sector, take);
        copied    += take;
        pos       += take;
        lba       += 1;
        in_sector  = 0;
    }

    *ppos = pos;
    return (ssize_t)copied;
}

static const file_ops_t ISO_FILE_FOPS = {
    .open       = iso_file_open,
    .release    = iso_file_release,
    .read       = iso_file_read,
    .write      = NULL,
    .fsync      = NULL,
    .ioctl      = NULL,
    .llseek     = NULL,
    .getdents64 = NULL,
};

/* ===== directory file_ops ===== */
static ssize_t iso_dir_getdents64(struct file *dirf, void *buf, size_t bytes) {
    if (!dirf || !buf) return -1;

    iso_inode_t *dip = (iso_inode_t*)dirf->f_inode->i_private;
    if (!dip || !dip->is_dir) return -ENOTDIR;

    const uint32_t bs   = dirf->f_inode->i_sb ? dirf->f_inode->i_sb->block_size : 2048;
    const uint32_t base = dip->extent_lba;
    const uint32_t dsz  = dip->extent_size;

    uint64_t pos = dirf->f_pos;
    size_t written = 0;

    while (pos < dsz) {
        uint32_t si = (uint32_t)(pos / bs);
        uint32_t so = (uint32_t)(pos % bs);

        uint8_t sec[2048];
        if (!iso_read_sector(&dip->fs->iso, base + si, sec)) {
            return (written > 0) ? (ssize_t)written : -EIO;
        }

        if (so >= bs) { pos = (uint64_t)(si + 1) * bs; continue; }

        uint8_t *rec = sec + so;
        uint8_t  len = rec[0];

        if (len == 0) { pos = (uint64_t)(si + 1) * bs; continue; }
        if (so + len > bs) { pos = (uint64_t)(si + 1) * bs; continue; }

        uint8_t  id_len   = rec[32];
        const uint8_t *id = rec + 33;

        uint32_t extent_lba = rd_le32(rec + 2);
        uint32_t data_len   = rd_le32(rec + 10);
        uint8_t  flags      = rec[25];

        // Convert ISO identifier -> C string
        char name[256];
        if (id_len == 1 && id[0] == 0) {
            strcpy(name, ".");
        } else if (id_len == 1 && id[0] == 1) {
            strcpy(name, "..");
        } else {
            iso_primary_ident_to_cstr(id, id_len, name, sizeof name);
            trim_version_semicolon(name);
        }

        uint8_t dtype = (flags & 0x02) ? VFS_DT_DIR : VFS_DT_REG;

        // Pack one vfs_dirent64_t into caller buffer
        size_t need = offsetof(vfs_dirent64_t, d_name) + strlen(name) + 1;
        if (written + need > bytes) break;

        vfs_dirent64_t *de = (vfs_dirent64_t *)((uint8_t*)buf + written);
        de->d_ino    = extent_lba;             // use extent start as stable id
        de->d_off    = (int64_t)(pos + len);   // next record position
        de->d_reclen = (uint16_t)need;
        de->d_type   = dtype;
        memcpy(de->d_name, name, strlen(name) + 1);

        written += need;
        pos     += len; // next record
        (void)data_len; // not used here
    }

    dirf->f_pos = pos;
    return (written > 0) ? (ssize_t)written : 0;  // 0 => EOF
}

static int iso_dir_open(struct inode *inode, struct file **out, int flags, uint32_t mode) {
    (void)mode;
    if (!inode || !out) return -1;
    if (!(flags & VFS_O_DIRECTORY)) return -ENOTDIR;

    iso_inode_t *ip = (iso_inode_t*)inode->i_private;
    if (!ip || !ip->is_dir) return -ENOTDIR;

    struct file *f = (struct file*)calloc(1, sizeof *f);
    if (!f) return -1;
    f->f_inode = inode;
    f->f_flags = flags;
    f->f_pos   = 0;
    *out = f;
    return 0;
}

/* ===== inode_ops ===== */

/* lookup: build a new inode from an ISO dir entry (Primary) */
static int iso_lookup(struct inode *dir, const char *name, struct inode **out) {
    if (out) *out = NULL;
    if (!dir || !name || !out) return -1;

    iso_inode_t *dip = (iso_inode_t*)dir->i_private;
    if (!dip || !dip->is_dir) return -1;

    uint32_t lba = 0, size = 0;
    uint8_t  flags = 0;

    int rc = iso_walk_component(&dip->fs->iso,
                                dip->extent_lba, dip->extent_size,
                                name, &lba, &size, &flags);
    if (rc != 1) {
        DBG("iso_lookup: '%s' not found rc=%d", name, rc);
        return -ENOENT;
    }

    const bool is_dir = (flags & 0x02) != 0;

    inode_t *child = (inode_t*)calloc(1, sizeof *child);
    iso_inode_t *cip = (iso_inode_t*)calloc(1, sizeof *cip);
    if (!child || !cip) { free(child); free(cip); return -ENOMEM; }

    cip->fs          = dip->fs;
    cip->is_dir      = is_dir;
    cip->extent_lba  = lba;
    cip->extent_size = size;

    child->i_ino     = lba; // stable id: start LBA
    child->i_mode    = (is_dir ? VFS_S_IFDIR : VFS_S_IFREG) |
                       (is_dir ? VFS_MODE_DIR_0755 : VFS_MODE_FILE_0644);
    child->i_sb      = dir->i_sb;
    child->i_op      = dir->i_op; // same ops table
    child->i_private = cip;

    if (is_dir) {
        static const file_ops_t FOPS_DIR = {
            .open       = iso_dir_open,
            .release    = NULL,
            .read       = NULL,
            .write      = NULL,
            .fsync      = NULL,
            .ioctl      = NULL,
            .llseek     = NULL,
            .getdents64 = iso_dir_getdents64,
        };
        child->i_fop = &FOPS_DIR;
    } else {
        child->i_fop = &ISO_FILE_FOPS;
    }

    *out = child;

    DBG("iso_lookup: name='%s' -> lba=%u size=%u flags=0x%02X (%s)",
        name, lba, size, flags, is_dir ? "DIR" : "FILE");
    return 0;
}

static int iso_getattr(struct inode *inode, struct g_stat *st) {
    if (!inode || !st) return -1;
    memset(st, 0, sizeof *st);
    iso_inode_t *ip = (iso_inode_t*)inode->i_private;
    if (!ip) return -1;

    st->st_mode  = inode->i_mode;
    st->st_ino   = inode->i_ino;
    st->st_nlink = 1;
    st->st_size  = ip->extent_size;
    st->st_blksize = inode->i_sb ? inode->i_sb->block_size : 2048;
    return 0;
}

static const inode_ops_t ISO_IOPS = {
    .lookup   = iso_lookup,
    .mkdir    = NULL, .rmdir = NULL, .unlink = NULL, .rename = NULL,
    .getattr  = iso_getattr,
    .setattr  = NULL,
    .truncate = NULL,
    .symlink  = NULL,
    .readlink = NULL,
};

/* ===== probe / mount / umount ===== */

static bool iso_probe(vblk_t *dev, char *label_out, size_t label_cap) {
    iso9660_t tmp;
    bool ok = iso_mount(dev, &tmp);
    if (ok && label_out && label_cap) label_out[0] = '\0'; // (optional: fill label)
    return ok;
}

static int iso_mount_fs(vblk_t *dev, const char *opts, superblock_t **out_sb) {
    (void)opts;
    if (!out_sb) return -1;
    *out_sb = NULL;

    DBG("iso_mount_fs: enter dev=%p opts='%s'", (void*)dev, opts ? opts : "");

    iso_fs_t *fs = (iso_fs_t*)calloc(1, sizeof *fs);
    if (!fs) { DBG("iso_mount_fs: calloc(fs) failed"); return -1; }
    if (!iso_mount(dev, &fs->iso)) {
        DBG("iso_mount_fs: iso_mount failed");
        free(fs);
        return -1;
    }

    DBG("iso_mount_fs: iso_mount OK root=[lba=%u size=%u] bs=%u",
        fs->iso.root_lba, fs->iso.root_size, fs->iso.block_size);

    superblock_t *sb = (superblock_t*)calloc(1, sizeof *sb);
    if (!sb) { DBG("iso_mount_fs: calloc(sb) failed"); free(fs); return -1; }

    inode_t *root = (inode_t*)calloc(1, sizeof *root);
    if (!root) { DBG("iso_mount_fs: calloc(root) failed"); free(sb); free(fs); return -1; }

    iso_inode_t *rip = (iso_inode_t*)calloc(1, sizeof *rip);
    if (!rip) { DBG("iso_mount_fs: calloc(rip) failed"); free(root); free(sb); free(fs); return -1; }

    rip->fs          = fs;
    rip->is_dir      = true;
    rip->extent_lba  = fs->iso.root_lba;
    rip->extent_size = fs->iso.root_size;

    root->i_ino     = rip->extent_lba; // handy for debugging
    root->i_mode    = VFS_S_IFDIR | VFS_MODE_DIR_0755;
    root->i_sb      = sb;
    root->i_op      = &ISO_IOPS;
    root->i_private = rip;

    static const file_ops_t FOPS_DIR = {
        .open       = iso_dir_open,
        .release    = NULL,
        .read       = NULL,
        .write      = NULL,
        .fsync      = NULL,
        .ioctl      = NULL,
        .llseek     = NULL,
        .getdents64 = iso_dir_getdents64,
    };
    root->i_fop = &FOPS_DIR;

    static const super_ops_t SOP = {
        .statfs = iso_statfs,
        .syncfs = iso_syncfs,
        .kill_sb= iso_kill_sb,
    };

    sb->fs_type = (struct filesystem_type *)&VFS_ISO9660;
    sb->bdev       = dev;
    sb->block_size = fs->iso.block_size ? fs->iso.block_size : 2048;   // reflect PVD, default 2048
    sb->root       = root;
    sb->s_op       = &SOP;
    sb->fs_private = fs;
    sb->s_flags   |= VFS_SB_RDONLY;  // enforce read-only at VFS layer

    DBG("iso9660: mounted (read-only), root=[lba=%u size=%u] bs=%u",
        rip->extent_lba, rip->extent_size, sb->block_size);

    *out_sb = sb;
    DBG("iso_mount_fs: success sb=%p root=%p bs=%u",
        (void*)sb, (void*)sb->root, sb->block_size);
    return 0;
}

static void iso_umount_fs(superblock_t *sb) { iso_kill_sb(sb); }

/* ===== exported driver symbol ===== */
const filesystem_type_t VFS_ISO9660 = {
    .name   = "iso9660",
    .probe  = iso_probe,
    .mount  = iso_mount_fs,
    .umount = iso_umount_fs,
};
