// src/cmd_cp.c
#include "cmds.h"
#include "vfs.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int cmd_cp(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: cp <src> <dst>\n");
        return 2;
    }
    const char *src = argv[1];
    const char *dst = argv[2];

    // Enforce your current rule: dst must be "/name"
    if (!(dst && dst[0] == '/' && dst[1] != '\0')) {
        fprintf(stderr, "cp: dst must be /<name>\n");
        return 2;
    }
    for (const char *p = dst + 1; *p; ++p) {
        if (*p == '/') {
            fprintf(stderr, "cp: dst must be /<name> (no subdirs yet)\n");
            return 2;
        }
    }

    // Read the whole source via VFS (ISO path expected per your design)
    // Try a doubling buffer to avoid knowing size up-front.
    uint32_t cap = 64 * 1024;
    uint8_t *buf = (uint8_t*)malloc(cap);
    if (!buf) { fprintf(stderr, "cp: OOM\n"); return 1; }

    uint32_t got = 0, total = 0;
    for (;;) {
        if (!vfs_read_all(src, buf, cap, &got)) {
            // If file larger than cap, grow and retry once (or loop)
            // Simple scheme: if got==cap, grow and try again, else fail.
            if (got == cap) {
                uint32_t ncap = cap * 2;
                uint8_t *nb = (uint8_t*)realloc(buf, ncap);
                if (!nb) { fprintf(stderr, "cp: OOM growing buffer\n"); free(buf); return 1; }
                buf = nb; cap = ncap;
                // try again with the larger buffer
                if (!vfs_read_all(src, buf, cap, &got)) {
                    fprintf(stderr, "cp: read failed for '%s'\n", src);
                    free(buf);
                    return 1;
                }
            } else {
                fprintf(stderr, "cp: read failed for '%s'\n", src);
                free(buf);
                return 1;
            }
        }
        total = got;
        break;
    }

    // Write to ext2 root
    if (!vfs_write_ext2_root(dst, buf, total)) {
        fprintf(stderr, "cp: write failed for '%s'\n", dst);
        free(buf);
        return 1;
    }

    free(buf);
    printf("cp: %u bytes -> %s\n", total, dst);
    return 0;
}
