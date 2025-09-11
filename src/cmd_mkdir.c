// src/cmd_mkdir.c

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "cmds.h"
#include "vfs.h"

/* mkdir <path> */
int cmd_mkdir(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: mkdir <path>\n");
        return 2;
    }

    const char *path = argv[1];
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "mkdir: invalid path\n");
        return 2;
    }

    if (!vfs_mkdir(path)) {
        fprintf(stderr, "mkdir: failed '%s'\n", path);
        return 1;
    }

    printf("mkdir: created '%s'\n", path);
    return 0;
}
