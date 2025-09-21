// src/vfs.c — VFS registry + single mount table + path router

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vfs.h"
#include "vfs_stat.h"

/* ---------- Feature toggles ---------- */
#ifndef VFS_HAVE_CREATE_OP
#define VFS_HAVE_CREATE_OP 0
#endif

#ifndef VFS_MAX_FS_TYPES
#define VFS_MAX_FS_TYPES 32
#endif

#ifndef VFS_MAX_MOUNTS
#define VFS_MAX_MOUNTS   32
#endif

#ifndef VFS_PATH_MAX
#define VFS_PATH_MAX     1024
#endif

/* ---------- Filesystem registry ---------- */

typedef struct { const char *name; const filesystem_type_t *fs; } fs_entry_t;
static fs_entry_t g_fs[VFS_MAX_FS_TYPES];
static int g_fs_n = 0;

int vfs_register(const filesystem_type_t *fst) {
    if (!fst || !fst->name) return -1;
    if (g_fs_n >= VFS_MAX_FS_TYPES) return -1;
    g_fs[g_fs_n++] = (fs_entry_t){ fst->name, fst };
    return 0;
}

int vfs_for_each_fs(vfs_fs_iter_cb cb, void *user) {
    if (!cb) return -1;
    for (int i = 0; i < g_fs_n; ++i) {
        int r = cb(g_fs[i].fs, user);
        if (r) return r;
    }
    return 0;
}

int vfs_register_alias(const char *alias, const filesystem_type_t *target) {
    if (!alias || !target) return -1;
    if (g_fs_n >= VFS_MAX_FS_TYPES) return -1;
    g_fs[g_fs_n++] = (fs_entry_t){ alias, target };
    return 0;
}

const filesystem_type_t* vfs_find_fs(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_fs_n; ++i) {
        const char *a = g_fs[i].name, *b = name;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
            if (ca != cb) break;
            ++a; ++b;
        }
        if (*a == '\0' && *b == '\0') return g_fs[i].fs;
    }
    return NULL;
}

/* ---------- Mount table (single source of truth) ---------- */

typedef struct mount_rec {
    char          mp[VFS_PATH_MAX]; /* normalized mountpoint, no trailing slash (except "/") */
    superblock_t *sb;
    /* user-visible metadata */
    char          src[64];          /* "/dev/a1" */
    char          fstype[16];       /* "ext2", "fat", ... */
    char          opts[64];         /* "rw", "ro,noexec", ... */
} mount_rec_t;

static mount_rec_t g_mnt[VFS_MAX_MOUNTS];
static int         g_mnt_n = 0;

/* Normalize path: convert '\' to '/', collapse '//' and trim trailing '/', keep "/" */
static void vfs_normalize_path(const char *in, char *out, size_t cap) {
    if (!in || !*in) { strncpy(out, "/", cap); out[cap-1] = '\0'; return; }
    size_t j = 0; char prev = 0;
    for (size_t i = 0; in[i] && j + 1 < cap; ++i) {
        char c = in[i];
        if (c == '\\') c = '/';
        if (c == '/' && prev == '/') continue;
        out[j++] = c; prev = c;
    }
    out[j] = '\0';
    size_t n = strlen(out);
    while (n > 1 && out[n-1] == '/') { out[n-1] = '\0'; --n; }
}

/* Longest-prefix match mount for given normalized path; returns index or -1 */
static int vfs_find_mount_for(const char *path_norm) {
    int best = -1; size_t best_len = 0;
    for (int i = 0; i < g_mnt_n; ++i) {
        const char *mp = g_mnt[i].mp;
        size_t mlen = strlen(mp);
        if (mlen == 1 && mp[0] == '/') {
            if (best == -1) { best = i; best_len = 1; }
            continue;
        }
        if (strncmp(path_norm, mp, mlen) == 0) {
            if (path_norm[mlen] == '\0' || path_norm[mlen] == '/') {
                if (mlen > best_len) { best = i; best_len = mlen; }
            }
        }
    }
    return best;
}

static void vfs_mount_relative(const char *path_norm, const mount_rec_t *m, char *rel, size_t cap) {
    size_t ml = strlen(m->mp);
    const char *p = (ml == 1 && m->mp[0] == '/') ? path_norm : path_norm + ml;
    while (*p == '/') ++p;
    strncpy(rel, p, cap); rel[cap-1] = '\0';
}

/* ---------- Path walk ---------- */

typedef struct path_res {
    inode_t *dir;
    inode_t *node;
    char     leaf[256];
    mount_rec_t *mnt;
} path_res_t;

/* Tokenize a relative path into components */
static const char* comp_next(const char *s, char name[256]) {
    size_t j = 0;
    while (*s && *s != '/' && j + 1 < 256) name[j++] = *s++;
    name[j] = '\0';
    while (*s == '/') ++s;
    return s;
}

static int vfs_walk_rel(mount_rec_t *mnt, const char *rel, path_res_t *out) {
    if (!mnt || !mnt->sb || !mnt->sb->root) return -1;
    inode_t *cur = mnt->sb->root;
    inode_t *parent = cur;
    char leaf[256] = {0};

    if (!rel || !*rel) {
        out->dir = cur;
        out->node = cur;
        out->leaf[0] = '\0';
        out->mnt = mnt;
        return 0;
    }

    char comp[256];
    const char *p = rel;
    while (*p) {
        parent = cur;
        p = comp_next(p, comp);
        if (comp[0] == '\0') break;

        if (!cur->i_op || !cur->i_op->lookup) return -1;
        inode_t *next = NULL;
        if (cur->i_op->lookup(cur, comp, &next) != 0) return -1;

        if (!next) {
            strncpy(leaf, comp, sizeof leaf); leaf[sizeof leaf - 1] = '\0';
            out->dir  = cur;
            out->node = NULL;
            out->mnt  = mnt;
            strncpy(out->leaf, leaf, sizeof out->leaf); out->leaf[sizeof out->leaf - 1] = '\0';
            return 0;
        }
        cur = next;
    }

    out->dir  = parent;
    out->node = cur;
    out->leaf[0] = '\0';
    out->mnt  = mnt;
    return 0;
}

static int vfs_resolve_path(const char *path, path_res_t *out) {
    if (!path || !*path) return -1;
    char norm[VFS_PATH_MAX];
    vfs_normalize_path(path, norm, sizeof norm);
    int mi = vfs_find_mount_for(norm);
    if (mi < 0) return -1;
    char rel[VFS_PATH_MAX];
    vfs_mount_relative(norm, &g_mnt[mi], rel, sizeof rel);
    return vfs_walk_rel(&g_mnt[mi], rel, out);
}

/* ---------- Router: mount / umount / list ---------- */

int vfs_mount_dev(const char *fstype,
                  const char *src,
                  vblk_t *dev,
                  const char *mountpoint,
                  const char *opts)
{
    if (!fstype || !src || !dev || !mountpoint) return -1;
    const filesystem_type_t *fs = vfs_find_fs(fstype);
    if (!fs || !fs->mount) return -1;

    char mp[VFS_PATH_MAX];
    vfs_normalize_path(mountpoint, mp, sizeof mp);

    for (int i = 0; i < g_mnt_n; ++i)
        if (strcmp(g_mnt[i].mp, mp) == 0) return -1; /* already mounted here */

    if (g_mnt_n >= VFS_MAX_MOUNTS) return -1;

    superblock_t *sb = NULL;
    int rc = fs->mount(dev, opts ? opts : "", &sb);
    if (rc != 0 || !sb) return -1;

    mount_rec_t *m = &g_mnt[g_mnt_n++];
    strncpy(m->mp, mp, sizeof m->mp); m->mp[sizeof m->mp - 1] = '\0';
    m->sb = sb;
    snprintf(m->src,    sizeof m->src,    "%s", src);
    snprintf(m->fstype, sizeof m->fstype, "%s", fstype);
    snprintf(m->opts,   sizeof m->opts,   "%s", (opts && *opts) ? opts : "rw");
    return 0;
}

int vfs_umount(const char *mountpoint) {
    if (!mountpoint) return -1;
    char mp[VFS_PATH_MAX];
    vfs_normalize_path(mountpoint, mp, sizeof mp);

    for (int i = 0; i < g_mnt_n; ++i) {
        if (strcmp(g_mnt[i].mp, mp) == 0) {
            superblock_t *sb = g_mnt[i].sb;
            for (int j = i + 1; j < g_mnt_n; ++j) g_mnt[j - 1] = g_mnt[j];
            g_mnt_n--;

            if (sb) {
                if (sb->s_op && sb->s_op->syncfs) (void)sb->s_op->syncfs(sb);
                if (sb->s_op && sb->s_op->kill_sb) sb->s_op->kill_sb(sb);
                else if (sb->fs_type && sb->fs_type->umount) sb->fs_type->umount(sb);
            }
            return 0;
        }
    }
    return -1;
}

void vfs_list_mounts(void) {
    if (g_mnt_n == 0) { puts("(no mounts)"); return; }
    for (int i = 0; i < g_mnt_n; ++i) {
        printf("%-10s %-6s %-12s %s\n",
               g_mnt[i].src[0] ? g_mnt[i].src : "-",
               g_mnt[i].fstype[0] ? g_mnt[i].fstype :
                 (g_mnt[i].sb && g_mnt[i].sb->fs_type && g_mnt[i].sb->fs_type->name ? g_mnt[i].sb->fs_type->name : "-"),
               g_mnt[i].mp,
               g_mnt[i].opts[0] ? g_mnt[i].opts : "-");
    }
}

/* Compatibility helpers for older callers (operate on unified table) */
int  vfs_register_mount(const char *src, const char *fstype,
                        const char *target, const char *opts)
{
    if (!src || !fstype || !target) return -1;
    char mp[VFS_PATH_MAX];
    vfs_normalize_path(target, mp, sizeof mp);
    for (int i = 0; i < g_mnt_n; ++i) {
        if (strcmp(g_mnt[i].mp, mp) == 0) {
            snprintf(g_mnt[i].src,    sizeof g_mnt[i].src,    "%s", src);
            snprintf(g_mnt[i].fstype, sizeof g_mnt[i].fstype, "%s", fstype);
            snprintf(g_mnt[i].opts,   sizeof g_mnt[i].opts,   "%s", (opts && *opts) ? opts : "rw");
            return 0;
        }
    }
    return -2;
}

int  vfs_unrecord_mount_by_target(const char *target)
{
    if (!target) return -1;
    char mp[VFS_PATH_MAX];
    vfs_normalize_path(target, mp, sizeof mp);
    for (int i = 0; i < g_mnt_n; ++i) {
        if (strcmp(g_mnt[i].mp, mp) == 0) {
            for (int j = i + 1; j < g_mnt_n; ++j) g_mnt[j - 1] = g_mnt[j];
            g_mnt_n--;
            return 0;
        }
    }
    return -2;
}

int  vfs_mount_count(void) { return g_mnt_n; }

const vfs_mount_t *vfs_mount_get(int index)
{
    static vfs_mount_t view;
    if (index < 0 || index >= g_mnt_n) return NULL;
    snprintf(view.src,    sizeof view.src,    "%s", g_mnt[index].src[0] ? g_mnt[index].src : "-");
    snprintf(view.fstype, sizeof view.fstype, "%s",
        g_mnt[index].fstype[0] ? g_mnt[index].fstype :
        (g_mnt[index].sb && g_mnt[index].sb->fs_type && g_mnt[index].sb->fs_type->name ? g_mnt[index].sb->fs_type->name : "-"));
    snprintf(view.target, sizeof view.target, "%s", g_mnt[index].mp);
    snprintf(view.opts,   sizeof view.opts,   "%s", g_mnt[index].opts[0] ? g_mnt[index].opts : "-");
    return &view;
}

/* ---------- File-level ops ---------- */

int vfs_open(const char *path, int flags, uint32_t mode, struct file **out) {
    if (!out) return -1;
    *out = NULL;

    path_res_t r;
    if (vfs_resolve_path(path, &r) != 0) return -1;
    if (!r.dir || !r.dir->i_op) return -1;

    inode_t *target = r.node;

#if VFS_HAVE_CREATE_OP
    if (!target && (flags & VFS_O_CREAT)) {
        if (!r.dir->i_op->create) return -1;
        if (r.leaf[0] == '\0')    return -1;
        if (r.dir->i_op->create(r.dir, r.leaf, mode, &target) != 0 || !target) return -1;
    }
#else
    if (!target) {
        if (flags & VFS_O_CREAT) return -1;
        return -1;
    }
#endif

    if ((flags & VFS_O_DIRECTORY) && !VFS_S_ISDIR(target->i_mode)) return -1;
    if (!target->i_fop || !target->i_fop->open) return -1;

    struct file *f = NULL;
    if (target->i_fop->open(target, &f, flags, mode) != 0 || !f) return -1;

    if ((flags & VFS_O_TRUNC) && r.dir->i_op && r.dir->i_op->truncate && VFS_S_ISREG(target->i_mode)) {
        (void)r.dir->i_op->truncate(target, 0);
    }

    *out = f;
    return 0;
}

int vfs_close(struct file *f) {
    if (!f) return 0;
    if (f->f_op && f->f_op->release) return f->f_op->release(f);
    return 0;
}

ssize_t vfs_read(struct file *f, void *buf, size_t n) {
    if (!f || !f->f_op || !f->f_op->read) return -1;
    return f->f_op->read(f, buf, n, &f->f_pos);
}

ssize_t vfs_write(struct file *f, const void *buf, size_t n) {
    if (!f || !f->f_op || !f->f_op->write) return -1;
    return f->f_op->write(f, buf, n, &f->f_pos);
}

int vfs_mkdir(const char *path, unsigned mode) {
#if VFS_HAVE_CREATE_OP
    path_res_t r;
    if (vfs_resolve_path(path, &r) != 0) return -1;
    if (!r.dir || !r.dir->i_op || !r.dir->i_op->mkdir) return -1;
    return r.dir->i_op->mkdir(r.dir, r.leaf, mode);
#else
    (void)path; (void)mode;
    return -1;
#endif
}

/* Metadata */
int vfs_stat(const char *path, struct g_stat *st) {
    if (!st) return -1;
    path_res_t r;
    if (vfs_resolve_path(path, &r) != 0) return -1;
    if (!r.node || !r.node->i_op || !r.node->i_op->getattr) return -1;
    return r.node->i_op->getattr(r.node, st);
}

int vfs_statfs(const char *path, struct g_statvfs *svfs) {
    if (!svfs) return -1;
    path_res_t r;
    if (vfs_resolve_path(path, &r) != 0) return -1;
    if (!r.mnt || !r.mnt->sb || !r.mnt->sb->s_op || !r.mnt->sb->s_op->statfs) return -1;
    return r.mnt->sb->s_op->statfs(r.mnt->sb, svfs);
}
