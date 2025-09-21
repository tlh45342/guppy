// cmd_cp.c — minimal cp built on the VFS router
// Copies a single regular file to a file or into an existing directory.
//
// Usage: cp <src> <dst>
// Notes: no -r; if src is a directory, returns error.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "vfs.h"       // VFS_O_*, VFS_S_* helpers, vfs_open/read/write/close/stat
#include "vfs_stat.h"  // struct g_stat (st_mode, etc.)

/* ---- Helpers ---- */

static bool path_is_directory(const char *path) {
    struct g_stat st;
    if (vfs_stat(path, &st) != 0) return false;
    return VFS_S_ISDIR(st.st_mode);
}

/* Return last path component; treats both '/' and '\\' as separators */
static const char* path_basename(const char *p) {
    if (!p || !*p) return p ? p : "";
    const char *slash = strrchr(p, '/');
    const char *bslash = strrchr(p, '\\');
    const char *sep = slash;
    if (bslash && (!sep || bslash > sep)) sep = bslash;
    return sep ? sep + 1 : p;
}

/* Join dir + file with a single '/' (malloc). Caller must free. */
static char* join_dir_file(const char *dir, const char *file) {
    size_t dl = strlen(dir), fl = strlen(file);
    int need_sep = (dl > 0 && dir[dl-1] != '/' && dir[dl-1] != '\\');
    size_t out_len = dl + (need_sep ? 1 : 0) + fl + 1;
    char *out = (char*)malloc(out_len);
    if (!out) return NULL;
    if (need_sep) snprintf(out, out_len, "%s/%s", dir, file);
    else          snprintf(out, out_len, "%s%s",    dir, file);
    return out;
}

/* ---- Command ---- */

int cmd_cp(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: cp <src> <dst>\n");
        return 1;
    }
    const char *src = argv[1];
    const char *dst = argv[2];

    /* Disallow directory-as-source (no -r support in shim) */
    if (path_is_directory(src)) {
        fprintf(stderr, "cp: -r not implemented; '%s' is a directory\n", src);
        return 1;
    }

    /* If destination is an existing directory, append basename(src) */
    char *final_alloc = NULL;
    const char *final_dst = dst;
    if (path_is_directory(dst)) {
        const char *base = path_basename(src);
        final_alloc = join_dir_file(dst, base);
        if (!final_alloc) {
            fprintf(stderr, "cp: out of memory\n");
            return 1;
        }
        final_dst = final_alloc;
    }

    /* Open source for reading */
    struct file *in = NULL;
    if (vfs_open(src, VFS_O_RDONLY, 0, &in) != 0 || !in) {
        fprintf(stderr, "cp: cannot open '%s' for read\n", src);
        free(final_alloc);
        return 1;
    }

    /* Open/create destination for writing (0644) */
    struct file *out = NULL;
    if (vfs_open(final_dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644, &out) != 0 || !out) {
        fprintf(stderr, "cp: cannot open '%s' for write\n", final_dst);
        vfs_close(in);
        free(final_alloc);
        return 1;
    }

    /* Copy loop */
    char buf[64 * 1024];
    for (;;) {
        ssize_t n = vfs_read(in, buf, sizeof buf);
        if (n < 0) {
            fprintf(stderr, "cp: read error on '%s'\n", src);
            vfs_close(in); vfs_close(out); free(final_alloc);
            return 1;
        }
        if (n == 0) break; /* EOF */

        char *p = buf;
        ssize_t remain = n;
        while (remain > 0) {
            ssize_t w = vfs_write(out, p, (size_t)remain);
            if (w < 0) {
                fprintf(stderr, "cp: write error on '%s'\n", final_dst);
                vfs_close(in); vfs_close(out); free(final_alloc);
                return 1;
            }
            remain -= w;
            p += w;
        }
    }

    vfs_close(in);
    vfs_close(out);
    free(final_alloc);
    return 0;
}