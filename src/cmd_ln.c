#include <stdio.h>
#include <string.h>
#include "cmds.h"
#include "vfs.h"
#include "logfile.h"

int cmd_ln(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[1], "-s") != 0) {
        guppy_eprintf("usage: ln -s <target> <link>\n");
        return 2;
    }
    if (vfs_symlink(argv[2], argv[3]) != 0) {
        guppy_eprintf("ln: cannot create symbolic link '%s' -> '%s'\n",
                      argv[3], argv[2]);
        return 1;
    }
    return 0;
}
