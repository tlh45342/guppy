// src/cmd_help.c

#include <stdio.h>
#include "cmd.h"

int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;  // silence unused warnings
    print_all_commands();
    return 0;
}