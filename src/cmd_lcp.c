// cmd_lcp.c — copy a local/host file into the mounted VFS
//
// Usage: lcp <local-src> <vfs-dst>
//
// The source path is resolved by the host OS (so Windows keeps its normal
// case-insensitive filename behavior).  The destination is resolved by Guppy's
// mounted VFS and therefore follows the mounted filesystem's semantics.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "vfs.h"
#include "vfs_stat.h"

static bool vfs_path_is_directory(const char *path)
{
    struct g_stat st;
    if (vfs_stat(path, &st) != 0)
        return false;
    return VFS_S_ISDIR(st.st_mode);
}

/* Host basename: accept either separator so scripts are portable. */
static const char *host_basename(const char *path)
{
    const char *slash;
    const char *bslash;
    const char *sep;

    if (!path || !*path)
        return path ? path : "";

    slash = strrchr(path, '/');
    bslash = strrchr(path, '\\');
    sep = slash;
    if (bslash && (!sep || bslash > sep))
        sep = bslash;

    return sep ? sep + 1 : path;
}

static char *join_vfs_path(const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    size_t nl = strlen(name);
    int need_sep = (dl > 0 && dir[dl - 1] != '/');
    size_t len = dl + (need_sep ? 1u : 0u) + nl + 1u;
    char *out = (char *)malloc(len);

    if (!out)
        return NULL;

    if (need_sep)
        snprintf(out, len, "%s/%s", dir, name);
    else
        snprintf(out, len, "%s%s", dir, name);

    return out;
}

int cmd_lcp(int argc, char **argv)
{
    const char *src;
    const char *dst;
    const char *final_dst;
    char *final_alloc = NULL;
    FILE *in = NULL;
    struct file *out = NULL;
    unsigned char buf[64 * 1024];
    int rc = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: lcp <local-src> <vfs-dst>\n");
        return 1;
    }

    src = argv[1];
    dst = argv[2];
    final_dst = dst;

    /*
     * IMPORTANT: fopen() is intentional here.  The host OS owns source-path
     * semantics.  On normal Windows filesystems, "boot.bin" can therefore
     * open "BOOT.BIN"; on a case-sensitive host it must match normally.
     */
    in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "lcp: cannot open local file '%s'\n", src);
        goto done;
    }

    /* "lcp foo /existing-dir" behaves naturally and appends foo's basename. */
    if (vfs_path_is_directory(dst)) {
        final_alloc = join_vfs_path(dst, host_basename(src));
        if (!final_alloc) {
            fprintf(stderr, "lcp: out of memory\n");
            goto done;
        }
        final_dst = final_alloc;
    }

    if (vfs_open(final_dst,
                 VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC,
                 0644,
                 &out) != 0 || !out) {
        fprintf(stderr, "lcp: cannot open VFS destination '%s'\n", final_dst);
        goto done;
    }

    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), in);

        if (n > 0) {
            size_t off = 0;
            while (off < n) {
                ssize_t w = vfs_write(out, buf + off, n - off);
                if (w <= 0) {
                    fprintf(stderr, "lcp: write error on '%s'\n", final_dst);
                    goto done;
                }
                off += (size_t)w;
            }
        }

        if (n < sizeof(buf)) {
            if (ferror(in)) {
                fprintf(stderr, "lcp: read error on local file '%s'\n", src);
                goto done;
            }
            break; /* EOF */
        }
    }

    /*
     * EXT2 currently performs important persistence work during close, so a
     * close failure must make lcp fail rather than reporting false success.
     */
    if (vfs_close(out) != 0) {
        out = NULL;
        fprintf(stderr, "lcp: close/flush failed for '%s'\n", final_dst);
        goto done;
    }
    out = NULL;

    rc = 0;

done:
    if (out)
        (void)vfs_close(out);
    if (in)
        fclose(in);
    free(final_alloc);
    return rc;
}
