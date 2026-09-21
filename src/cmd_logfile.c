#include <stdio.h>
#include <string.h>

#include "cmds.h"
#include "logfile.h"

static void usage(void)
{
    guppy_eprintf("usage: logfile [off|-a <file>|<file>]\n");
}

int cmd_logfile(int argc, char **argv)
{
    const char *path;
    int append = 0;

    if (argc == 1) {
        if (guppy_log_active())
            guppy_printf("logfile: %s\n", guppy_log_path());
        else
            guppy_printf("logfile: off\n");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        if (!guppy_log_active()) {
            guppy_printf("logfile: off\n");
            return 0;
        }
        guppy_printf("Logging stopped.\n");
        guppy_log_stop();
        return 0;
    }

    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "-a") == 0) {
        append = 1;
        path = argv[2];
    } else {
        usage();
        return 2;
    }

    if (guppy_log_start(path, append) != 0) {
        guppy_eprintf("logfile: cannot open '%s'\n", path);
        return 1;
    }

    guppy_printf("Logging to %s%s\n", path, append ? " (append)" : "");
    return 0;
}
