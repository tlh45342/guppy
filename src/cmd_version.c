// src/cmd_version.c
#include <stdio.h>
#include "version.h"
#include "app_printf.h"

int cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    app_printf("%s\n", VERSION);   // print just the version string
    return 0;
}
