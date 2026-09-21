// src/vfs.c — VFS registry + single mount table + path router

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include "debug.h"
#include "cwd.h"
#include "vfs.h"
#include "app_printf.h"
#include "vfs_stat.h"


#include <stdio.h>
#include <string.h>
#if defined(_MSC_VER)
#  define strcasecmp _stricmp
#else
#  include <strings.h>   // for strcasecmp on POSIX/Cygwin
#endif


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
    /* dedupe by name (case-insensitive) */
    for (int i = 0; i < g_fs_n; ++i) {
        if (strcasecmp(g_fs[i].name, fst->name) == 0) return 0;
    }
    g_fs[g_fs_n++] = (fs_entry_t){ fst->name, fst };
    DBG("vfs: registered '%s'", fst->name);
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
    DBG("vfs: alias '%s' -> '%s'", alias, target->name ? target->name : "?");
    return 0;
}

static int ci_cmp(char a, char b) {
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return (int)(unsigned char)a - (int)(unsigned char)b;
}

const filesystem_type_t* vfs_find_fs(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_fs_n; ++i) {
        const char *a = g_fs[i].name, *b = name;
        while (*a && *b && ci_cmp(*a, *b) == 0) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return g_fs[i].fs;
    }
    return NULL;
}

/* ---------- Mount table (single source of truth) ---------- */
typedef struct mount_rec {
    char          mp[VFS_PATH_MAX]; /* normalized mountpoint */
    superblock_t *sb;
    /* user-visible metadata */
    char          src[64];          /* "/dev/a1" */
    char          fstype[16];       /* "ext2", "fat", ... */
    char          opts[64];         /* "rw", "ro,noexec", ... */
} mount_rec_t;

static mount_rec_t g_mnt[VFS_MAX_MOUNTS];
static int         g_mnt_n = 0;

/*
 * Convert a user path into one canonical absolute VFS path.
 * Relative paths are resolved against Guppy's cwd before mount routing.
 */
static void vfs_normalize_path(const char *in, char *out, size_t cap) {
    char combined[VFS_PATH_MAX * 2];
    const char *cwd = cwd_get();

    if (!out || cap == 0) return;
    out[0] = '\0';

    if (!in || !*in) in = ".";
    if (!cwd || !*cwd || cwd[0] != '/') cwd = "/";

    if (in[0] == '/' || in[0] == '\\')
        snprintf(combined, sizeof combined, "%s", in);
    else if (strcmp(cwd, "/") == 0)
        snprintf(combined, sizeof combined, "/%s", in);
    else
        snprintf(combined, sizeof combined, "%s/%s", cwd, in);

    size_t oi = 0;
    out[oi++] = '/';
    out[oi] = '\0';

    size_t i = 0;
    while (combined[i]) {
        while (combined[i] == '/' || combined[i] == '\\') ++i;
        if (!combined[i]) break;

        size_t start = i;
        while (combined[i] && combined[i] != '/' && combined[i] != '\\') ++i;
        size_t seglen = i - start;

        if (seglen == 1 && combined[start] == '.') continue;

        if (seglen == 2 && combined[start] == '.' && combined[start + 1] == '.') {
            if (oi > 1) {
                while (oi > 1 && out[oi - 1] != '/') --oi;
                if (oi > 1) --oi;
                out[oi] = '\0';
            }
            continue;
        }

        if (oi > 1) {
            if (oi + 1 >= cap) { out[0] = '\0'; return; }
            out[oi++] = '/';
        }
        if (oi + seglen >= cap) { out[0] = '\0'; return; }
        memcpy(out + oi, combined + start, seglen);
        oi += seglen;
        out[oi] = '\0';
    }
}

/* Longest-prefix match mount */
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

static int vfs_path_set(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0 || !src) return -1;
    n = strlen(src);
    if (n >= cap) { dst[0] = '\0'; return -1; }
    memcpy(dst, src, n + 1);
    return 0;
}

static int vfs_path_append(char *dst, size_t cap, const char *part)
{
    size_t dl, pl;
    bool need_slash;
    if (!dst || !part || cap == 0) return -1;
    dl = strlen(dst);
    while (*part == '/' || *part == '\\') ++part;
    pl = strlen(part);
    if (pl == 0) return 0;
    need_slash = (dl == 0 || dst[dl - 1] != '/');
    if (dl + (need_slash ? 1u : 0u) + pl >= cap) return -1;
    if (need_slash) dst[dl++] = '/';
    memcpy(dst + dl, part, pl + 1);
    return 0;
}

/* Resolve a VFS pathname, following symbolic links encountered while walking.
 * Intermediate symbolic links are always followed.  The final component may
 * optionally be left unresolved for operations such as readlink(), unlink(),
 * and symlink creation that operate on the link inode itself.
 *
 * Resolution restarts from the VFS mount router after each symlink expansion.
 * This is slightly more work than continuing the current inode walk, but it
 * gives the correct behavior for absolute symlink targets and for links that
 * cross a Guppy mount point.
 */
static int vfs_resolve_path_ex(const char *path, path_res_t *out, bool follow_final)
{
    enum { VFS_SYMLINK_MAX = 40 };
    char current[VFS_PATH_MAX];
    int followed = 0;

    if (!path || !*path || !out) return -1;

    vfs_normalize_path(path, current, sizeof current);
    if (current[0] == '\0') return -1;

    for (;;) {
        int mi = vfs_find_mount_for(current);
        if (mi < 0) return -1;

        mount_rec_t *mnt = &g_mnt[mi];
        if (!mnt->sb || !mnt->sb->root) return -1;

        char rel[VFS_PATH_MAX];
        vfs_mount_relative(current, mnt, rel, sizeof rel);

        inode_t *cur = mnt->sb->root;
        inode_t *parent = cur;
        char leaf[256] = {0};

        if (rel[0] == '\0') {
            out->dir = cur;
            out->node = cur;
            out->leaf[0] = '\0';
            out->mnt = mnt;
            return 0;
        }

        /* Absolute VFS path corresponding to the directory containing the
         * component currently being looked up. */
        char parent_path[VFS_PATH_MAX];
        if (vfs_path_set(parent_path, sizeof parent_path, mnt->mp) != 0) return -1;

        const char *p = rel;
        bool restart = false;
        while (*p) {
            char comp[256];
            const char *rest = comp_next(p, comp);
            bool is_final = (*rest == '\0');

            if (comp[0] == '\0') {
                p = rest;
                continue;
            }

            if (!cur->i_op || !cur->i_op->lookup) return -1;

            inode_t *next = NULL;
            int rc = cur->i_op->lookup(cur, comp, &next);
            if (rc != 0) return -1;

            if (!next) {
                out->dir = cur;
                out->node = NULL;
                out->mnt = mnt;
                snprintf(out->leaf, sizeof out->leaf, "%s", comp);
                return 0;
            }

            snprintf(leaf, sizeof leaf, "%s", comp);

            if (VFS_S_ISLNK(next->i_mode) && (!is_final || follow_final)) {
                char target[VFS_PATH_MAX];
                if (!next->i_op || !next->i_op->readlink) return -1;
                int n = next->i_op->readlink(next, target, sizeof target - 1);
                if (n < 0 || (size_t)n >= sizeof target) return -1;
                target[n] = '\0';

                if (++followed > VFS_SYMLINK_MAX) {
                    DBG("vfs: symlink traversal limit exceeded for '%s'", path);
                    return -1;
                }

                char expanded[VFS_PATH_MAX * 3];
                expanded[0] = '\0';
                if (target[0] == '/' || target[0] == '\\') {
                    if (vfs_path_set(expanded, sizeof expanded, target) != 0) return -1;
                } else {
                    if (vfs_path_set(expanded, sizeof expanded, parent_path) != 0) return -1;
                    if (vfs_path_append(expanded, sizeof expanded, target) != 0) return -1;
                }
                if (*rest && vfs_path_append(expanded, sizeof expanded, rest) != 0) return -1;

                vfs_normalize_path(expanded, current, sizeof current);
                if (current[0] == '\0') return -1;
                DBG("vfs: symlink '%s' -> '%s'; restart as '%s'",
                    comp, target, current);
                restart = true;
                break;
            }

            parent = cur;
            cur = next;

            /* Extend the absolute containing-directory path for the next
             * component.  Do not append the final component because there is
             * no following lookup that needs it as a parent. */
            if (!is_final) {
                if (vfs_path_append(parent_path, sizeof parent_path, comp) != 0) return -1;
            }

            p = rest;
        }

        if (restart) continue;

        out->dir = parent;
        out->node = cur;
        snprintf(out->leaf, sizeof out->leaf, "%s", leaf);
        out->mnt = mnt;
        return 0;
    }
}

/* Historical/default resolver: normal pathname consumers follow the final
 * symlink just like POSIX stat/open/chdir. */
static int vfs_resolve_path(const char *path, path_res_t *out) {
    return vfs_resolve_path_ex(path, out, true);
}

static int vfs_resolve_path_nofinal(const char *path, path_res_t *out) {
    return vfs_resolve_path_ex(path, out, false);
}

/* ---------- Router: mount / umount / list ---------- */
int vfs_mount_dev(const char *fstype,
                  const char *src,
                  vblk_t *dev,
                  const char *mountpoint,
                  const char *opts)
{
    if (!fstype || !src || !dev || !mountpoint) {
        DBG("vfs: mount: invalid args fstype=%p src=%p dev=%p mp=%p", (void*)fstype, (void*)src, (void*)dev, (void*)mountpoint);
        return -1;
    }

    const filesystem_type_t *fs = vfs_find_fs(fstype);
    if (!fs || !fs->mount) {
        DBG("vfs: mount: unknown fs '%s' or missing mount()", fstype ? fstype : "(null)");
        return -1;
    }

    char mp[VFS_PATH_MAX];
    vfs_normalize_path(mountpoint, mp, sizeof mp);

    /* deny duplicate mountpoint */
    for (int i = 0; i < g_mnt_n; ++i) {
        if (strcmp(g_mnt[i].mp, mp) == 0) {
            DBG("vfs: mount: mountpoint '%s' already in use", mp);
            return -1;
        }
    }

    if (g_mnt_n >= VFS_MAX_MOUNTS) {
        DBG("vfs: mount: mount table full (%d >= %d)", g_mnt_n, VFS_MAX_MOUNTS);
        return -1;
    }

    superblock_t *sb = NULL;
    const char *mopts = (opts && *opts) ? opts : "";
    int rc = fs->mount(dev, mopts, &sb);
    DBG("vfs: fs->mount('%s','%s') rc=%d sb=%p", fstype, mopts, rc, (void*)sb);

    if (rc != 0 || !sb) {
        DBG("vfs: mount failed: rc=%d sb=%p", rc, (void*)sb);
        return -1;
    }
    if (!sb->root) {
        DBG("vfs: mount: filesystem returned NULL root");
        if (sb->s_op && sb->s_op->kill_sb) sb->s_op->kill_sb(sb);
        return -1;
    }

    /* record */
    mount_rec_t *m = &g_mnt[g_mnt_n++];
    strncpy(m->mp, mp, sizeof m->mp); m->mp[sizeof m->mp - 1] = '\0';
    m->sb = sb;
    snprintf(m->src,    sizeof m->src,    "%s", src);
    snprintf(m->fstype, sizeof m->fstype, "%s", fstype);

    /* if caller didn’t pass opts, reflect readonly flag */
    const bool ro = (sb->s_flags & VFS_SB_RDONLY) != 0;
    snprintf(m->opts, sizeof m->opts, "%s", (opts && *opts) ? opts : (ro ? "ro" : "rw"));

    DBG("vfs: mount '%s' on '%s' type='%s' opts='%s' (root=%p)",
        m->src, m->mp, m->fstype, m->opts, (void*)sb->root);

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
    if (g_mnt_n == 0) { app_printf("(no mounts)\n"); return; }
    for (int i = 0; i < g_mnt_n; ++i) {
        app_printf("%-10s %-6s %-12s %s\n",
               g_mnt[i].src[0] ? g_mnt[i].src : "-",
               g_mnt[i].fstype[0] ? g_mnt[i].fstype :
                 (g_mnt[i].sb && g_mnt[i].sb->fs_type && g_mnt[i].sb->fs_type->name ? g_mnt[i].sb->fs_type->name : "-"),
               g_mnt[i].mp,
               g_mnt[i].opts[0] ? g_mnt[i].opts : "-");
    }
}

/* Compatibility helpers for UI */
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
    if (!target) return -1;
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

int vfs_mknod(const char *path, uint32_t mode, uint32_t major, uint32_t minor) {
    path_res_t r;
    if (!path || !*path) return -1;
    if ((mode & VFS_S_IFMT) != VFS_S_IFCHR && (mode & VFS_S_IFMT) != VFS_S_IFBLK) return -1;
    if (vfs_resolve_path_nofinal(path, &r) != 0) return -1;
    if (!r.dir || !r.dir->i_op || !r.dir->i_op->mknod) return -1;
    if (r.node || r.leaf[0] == '\0') return -1;
    return r.dir->i_op->mknod(r.dir, r.leaf, mode, major, minor);
}

int vfs_symlink(const char *target, const char *linkpath) {
    path_res_t r;

    if (!target || !*target || !linkpath || !*linkpath) return -1;
    if (vfs_resolve_path_nofinal(linkpath, &r) != 0) return -1;
    if (!r.dir || !r.dir->i_op || !r.dir->i_op->symlink) return -1;
    if (r.node || r.leaf[0] == '\0') return -1;

    return r.dir->i_op->symlink(r.dir, r.leaf, target);
}

int vfs_unlink(const char *path) {
    path_res_t r;

    if (!path || !*path) return -1;
    if (vfs_resolve_path_nofinal(path, &r) != 0) return -1;
    if (!r.node || !r.dir || !r.dir->i_op || !r.dir->i_op->unlink) return -1;
    if (r.leaf[0] == '\0') return -1;
    if (VFS_S_ISDIR(r.node->i_mode)) return -1;

    return r.dir->i_op->unlink(r.dir, r.leaf);
}

/* ---------- Metadata ---------- */
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

int vfs_chmod(const char *path, uint32_t mode) {
    path_res_t r;
    vfs_iattr_t attr;

    if (!path || !*path) return -1;
    if (mode & ~07777u) return -1;
    if (vfs_resolve_path(path, &r) != 0) return -1;
    if (!r.node || !r.node->i_op || !r.node->i_op->setattr) return -1;

    memset(&attr, 0, sizeof attr);
    attr.valid = VFS_IATTR_MODE;
    attr.mode = mode & 07777u;
    return r.node->i_op->setattr(r.node, &attr);
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    path_res_t r;
    vfs_iattr_t attr;

    if (!path || !*path) return -1;
    if (vfs_resolve_path(path, &r) != 0) return -1;
    if (!r.node || !r.node->i_op || !r.node->i_op->setattr) return -1;

    memset(&attr, 0, sizeof attr);
    attr.valid = VFS_IATTR_UID | VFS_IATTR_GID;
    attr.uid = uid;
    attr.gid = gid;
    return r.node->i_op->setattr(r.node, &attr);
}

/* ---- Built-in FS declarations (provided by each module) ---- */
extern const filesystem_type_t VFS_EXT2;
extern const filesystem_type_t VFS_FAT;
extern const filesystem_type_t VFS_VFAT;
extern const filesystem_type_t VFS_NTFS;
extern const filesystem_type_t VFS_ISO9660;

int vfs_init(void) {
    (void)vfs_register(&VFS_EXT2);
    (void)vfs_register(&VFS_FAT);
    (void)vfs_register(&VFS_VFAT);
    (void)vfs_register(&VFS_NTFS);
    (void)vfs_register(&VFS_ISO9660);
    return 0;
}

int vfs_readlink(const char *path, char *buf, size_t bufsz) {
    if (!path || !buf || bufsz == 0) return -1;
    path_res_t r;
    if (vfs_resolve_path_nofinal(path, &r) != 0) return -1;
    if (!r.node || !r.node->i_op || !r.node->i_op->readlink) return -1;
    /* Contract: return number of bytes written (like readlink), and ensure NUL at caller if desired. */
    int n = r.node->i_op->readlink(r.node, buf, bufsz - 1);
    if (n < 0) return -1;
    if ((size_t)n < bufsz) buf[n] = '\0';  /* convenience for callers */
    return n;
}

ssize_t vfs_getdents64(struct file *f, void *buf, size_t bytes)
{
    ssize_t n;

    if (!f || !f->f_op || !f->f_op->getdents64)
        return -ENOTDIR;

    DBG("vfs:getdents64 enter pos=%llu cap=%llu",
        (unsigned long long)f->f_pos,
        (unsigned long long)bytes);

    n = f->f_op->getdents64(f, buf, bytes);

    DBG("vfs:getdents64 -> n=%lld newpos=%llu",
        (long long)n,
        (unsigned long long)f->f_pos);

    return n;
}

