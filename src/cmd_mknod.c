// src/cmd_mknod.c - create VFS character/block special files

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"

static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !*s || !out || *s == '-') return -1;
    errno = 0;
    v = strtoul(s, &end, 0);
    if (errno || !end || *end != '\0' || v > UINT32_MAX) return -1;
    *out = (uint32_t)v;
    return 0;
}

int cmd_mknod(int argc, char **argv)
{
    uint32_t major, minor, type;

    if (argc != 5 || (strcmp(argv[2], "c") != 0 && strcmp(argv[2], "b") != 0)) {
        fprintf(stderr, "usage: mknod <path> <c|b> <major> <minor>\n");
        return 1;
    }
    if (parse_u32(argv[3], &major) != 0 || parse_u32(argv[4], &minor) != 0) {
        fprintf(stderr, "mknod: major and minor must be non-negative integers\n");
        return 1;
    }

    type = (argv[2][0] == 'c') ? VFS_S_IFCHR : VFS_S_IFBLK;
    if (vfs_mknod(argv[1], type | 0666u, major, minor) != 0) {
        fprintf(stderr, "mknod: cannot create '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}
