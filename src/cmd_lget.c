// cmd_lget.c — copy a Guppy VFS file to the local/host filesystem
//
// Usage: lget <vfs-src> <local-dst>
//
// This is the inverse of lcp.  The source is resolved through Guppy's VFS;
// the destination is opened by the host OS.  It is intentionally useful for
// extracting files from read-only media such as the Guppy reference ISO.

#include <stdio.h>
#include <string.h>
#include "vfs.h"

int cmd_lget(int argc, char **argv)
{
    struct file *in = NULL;
    FILE *out = NULL;
    unsigned char buf[64 * 1024];
    int rc = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: lget <vfs-src> <local-dst>\n");
        return 1;
    }

    if (vfs_open(argv[1], VFS_O_RDONLY, 0, &in) != 0 || !in) {
        fprintf(stderr, "lget: cannot open VFS source '%s'\n", argv[1]);
        goto done;
    }

    out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "lget: cannot open local destination '%s'\n", argv[2]);
        goto done;
    }

    for (;;) {
        ssize_t n = vfs_read(in, buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "lget: read error on '%s'\n", argv[1]);
            goto done;
        }
        if (n == 0)
            break;
        if (fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
            fprintf(stderr, "lget: write error on local file '%s'\n", argv[2]);
            goto done;
        }
    }

    if (fflush(out) != 0 || fclose(out) != 0) {
        out = NULL;
        fprintf(stderr, "lget: close/flush failed for local file '%s'\n", argv[2]);
        goto done;
    }
    out = NULL;
    rc = 0;

done:
    if (out)
        fclose(out);
    if (in)
        (void)vfs_close(in);
    if (rc != 0)
        remove(argv[2]);
    return rc;
}
