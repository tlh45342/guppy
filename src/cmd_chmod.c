// cmd_chmod.c — change VFS file permission bits
// usage: chmod <octal-mode> <path>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vfs.h"

static int parse_mode(const char *s, unsigned *mode_out)
{
    char *end = NULL;
    unsigned long v;

    if (!s || !*s || !mode_out)
        return -1;

    errno = 0;
    v = strtoul(s, &end, 8);
    if (errno != 0 || !end || *end != '\0' || v > 07777ul)
        return -1;

    *mode_out = (unsigned)v;
    return 0;
}

int cmd_chmod(int argc, char **argv)
{
    unsigned mode;

    if (argc != 3) {
        fprintf(stderr, "usage: chmod <octal-mode> <path>\n");
        return 1;
    }

    if (parse_mode(argv[1], &mode) != 0) {
        fprintf(stderr, "chmod: invalid octal mode '%s'\n", argv[1]);
        return 1;
    }

    if (vfs_chmod(argv[2], mode) != 0) {
        fprintf(stderr, "chmod: cannot change mode of '%s'\n", argv[2]);
        return 1;
    }

    return 0;
}
