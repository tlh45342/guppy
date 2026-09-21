#include <stdio.h>
#include "cmds.h"
#include "vfs.h"
#include "logfile.h"

int cmd_readlink(int argc, char **argv)
{
    char buf[1024];
    int rc;
    if (argc != 2) {
        guppy_eprintf("usage: readlink <path>\n");
        return 2;
    }
    rc = vfs_readlink(argv[1], buf, sizeof buf);
    if (rc < 0) {
        guppy_eprintf("readlink: cannot read '%s'\n", argv[1]);
        return 1;
    }
    buf[sizeof buf - 1] = 0;
    guppy_printf("%s\n", buf);
    return 0;
}
