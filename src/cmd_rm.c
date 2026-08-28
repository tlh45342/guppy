#include <stdio.h>
#include "cmds.h"
#include "vfs.h"
#include "vfs_stat.h"

int cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: rm <file> [file...]\n");
        return 2;
    }

    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        struct g_stat st;
        if (vfs_stat(argv[i], &st) != 0) {
            fprintf(stderr, "rm: cannot remove '%s': no such file\n", argv[i]);
            rc = 1;
            continue;
        }
        if (VFS_S_ISDIR(st.st_mode)) {
            fprintf(stderr, "rm: cannot remove '%s': is a directory\n", argv[i]);
            rc = 1;
            continue;
        }
        if (vfs_unlink(argv[i]) != 0) {
            fprintf(stderr, "rm: cannot remove '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
