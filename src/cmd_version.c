// src/cmd_version.c
#include <stdio.h>
#include "version.h"

// Expect version.h to define VERSION or APP_VERSION (adjust if needed)
#ifndef VERSION
#  ifdef APP_VERSION
#    define VERSION APP_VERSION
#  else
#    define VERSION "0.0.0-dev"
#  endif
#endif

int cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    puts(VERSION);   // print just the version string
    return 0;
}
