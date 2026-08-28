// cmd_cp.c — copy one Guppy VFS file to another
//
// Usage: cp <src> <dst>
//
// Both source and destination are Guppy VFS paths.  Local/host -> VFS copying
// is intentionally handled by lcp.
//
// Correctness rule: cp does not report success until the destination has been
// closed successfully.  Filesystems such as EXT2 may defer allocation/writeback
// until close(), so a successful copy loop is not sufficient proof that the
// destination was committed.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "vfs.h"
#include "vfs_stat.h"

/* ---- Helpers ---- */

static bool path_is_directory(const char *path)
{
    struct g_stat st;
    if (vfs_stat(path, &st) != 0)
        return false;
    return VFS_S_ISDIR(st.st_mode);
}

/* Return last path component; accepts either separator. */
static const char *path_basename(const char *p)
{
    if (!p || !*p)
        return p ? p : "";

    const char *slash = strrchr(p, '/');
    const char *bslash = strrchr(p, '\\');
    const char *sep = slash;

    if (bslash && (!sep || bslash > sep))
        sep = bslash;

    return sep ? sep + 1 : p;
}

/* Join dir + file with one '/'. Caller frees the result. */
static char *join_dir_file(const char *dir, const char *file)
{
    size_t dl = strlen(dir);
    size_t fl = strlen(file);
    int need_sep = (dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\');
    size_t out_len = dl + (need_sep ? 1u : 0u) + fl + 1u;

    char *out = (char *)malloc(out_len);
    if (!out)
        return NULL;

    if (need_sep)
        snprintf(out, out_len, "%s/%s", dir, file);
    else
        snprintf(out, out_len, "%s%s", dir, file);

    return out;
}

/* ---- Command ---- */

int cmd_cp(int argc, char **argv)
{
    struct file *in = NULL;
    struct file *out = NULL;
    char *final_alloc = NULL;
    int rc = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: cp <src> <dst>\n");
        return 1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];
    const char *final_dst = dst;

    if (path_is_directory(src)) {
        fprintf(stderr, "cp: -r not implemented; '%s' is a directory\n", src);
        goto done;
    }

    /* Existing directory destination means dst/basename(src). */
    if (path_is_directory(dst)) {
        final_alloc = join_dir_file(dst, path_basename(src));
        if (!final_alloc) {
            fprintf(stderr, "cp: out of memory\n");
            goto done;
        }
        final_dst = final_alloc;
    }

    if (vfs_open(src, VFS_O_RDONLY, 0, &in) != 0 || !in) {
        fprintf(stderr, "cp: cannot open '%s' for read\n", src);
        goto done;
    }

    if (vfs_open(final_dst,
                 VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC,
                 0644,
                 &out) != 0 || !out) {
        fprintf(stderr, "cp: cannot open '%s' for write\n", final_dst);
        goto done;
    }

    {
        unsigned char buf[64 * 1024];

        for (;;) {
            ssize_t n = vfs_read(in, buf, sizeof buf);

            if (n < 0) {
                fprintf(stderr, "cp: read error on '%s'\n", src);
                goto done;
            }

            if (n == 0)
                break;

            size_t off = 0;
            while (off < (size_t)n) {
                ssize_t w = vfs_write(out, buf + off, (size_t)n - off);

                if (w < 0) {
                    fprintf(stderr, "cp: write error on '%s'\n", final_dst);
                    goto done;
                }

                /*
                 * A zero-length write while data remains would otherwise make
                 * this loop spin forever. Treat it as an I/O failure.
                 */
                if (w == 0) {
                    fprintf(stderr,
                            "cp: short write on '%s' (wrote 0 bytes)\n",
                            final_dst);
                    goto done;
                }

                off += (size_t)w;
            }
        }
    }

    /*
     * Close the source first.  The important commit point is destination close:
     * EXT2 currently performs buffered allocation/writeback from release().
     */
    if (vfs_close(in) != 0) {
        in = NULL;
        fprintf(stderr, "cp: close failed on source '%s'\n", src);
        goto done;
    }
    in = NULL;

    if (vfs_close(out) != 0) {
        out = NULL;
        fprintf(stderr,
                "cp: destination close/flush failed for '%s'\n",
                final_dst);
        goto done;
    }
    out = NULL;

    rc = 0;

done:
    /*
     * Cleanup failures are secondary here: if we reached this block because of
     * an earlier read/write error, preserve that original failure.  When out is
     * still open, close it so filesystem buffers/resources are released.
     */
    if (in) {
        (void)vfs_close(in);
        in = NULL;
    }

    if (out) {
        int crc = vfs_close(out);
        out = NULL;

        /*
         * If no earlier diagnostic established the failure reason, make a
         * deferred destination failure visible. rc is already nonzero here.
         */
        if (crc != 0)
            fprintf(stderr,
                    "cp: destination close/flush failed for '%s'\n",
                    final_dst);
    }

    free(final_alloc);
    return rc;
}
