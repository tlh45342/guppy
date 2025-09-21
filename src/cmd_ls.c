// src/cmd_ls.c — minimal ls on top of the VFS
#include <stdio.h>
#include "vfs.h"
#include "vfs_stat.h"   // <-- defines struct g_stat
#include "gu_dirent.h"  // <-- vfs_opendir/readdir/closedir wrapper

int cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";

    DIR *d = vfs_opendir(path);
    if (!d) {
        // Not a directory? If it exists, print the name and exit 0.
        struct g_stat st;
        if (vfs_stat(path, &st) == 0) { puts(path); return 0; }
        fprintf(stderr, "ls: cannot access '%s'\n", path);
        return 1;
    }

    for (struct dirent *de; (de = vfs_readdir(d)) != NULL; ) {
        // Skip "." and ".." (optional)
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        puts(de->d_name);
    }

    vfs_closedir(d);
    return 0;
}
