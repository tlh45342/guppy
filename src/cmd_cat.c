// src/cmd_cat.c — minimal cat using the VFS router
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "vfs.h"

int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: cat <path>\n");
        return 1;
    }
    const char *path = argv[1];

#ifdef _WIN32
    /* Avoid CRLF munging when printing binaries */
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    struct file *f = NULL;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0 || !f) {
        fprintf(stderr, "cat: cannot open '%s'\n", path);
        return 1;
    }

    char buf[64 * 1024];
    for (;;) {
        ssize_t n = vfs_read(f, buf, sizeof buf);
        if (n < 0) {
            fprintf(stderr, "cat: read error on '%s'\n", path);
            vfs_close(f);
            return 1;
        }
        if (n == 0) break; /* EOF */

        size_t w = fwrite(buf, 1, (size_t)n, stdout);
        if (w != (size_t)n) {
            fprintf(stderr, "cat: write error to stdout\n");
            vfs_close(f);
            return 1;
        }
    }

    vfs_close(f);
    return 0;
}
