// src/cmd_version.c
#include <stdio.h>
#include "version.h"

int cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    puts(VERSION);   // print just the version string
    return 0;
}
