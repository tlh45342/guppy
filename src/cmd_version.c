// src/cmd_version.c
#include <stdio.h>
#include "version.h"
#include "app_printf.h"

extern unsigned guppy_build_number(void);

int cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    app_printf("Guppy %s (Build %u)\n", VERSION, guppy_build_number());   // print just the version string
    return 0;
}
