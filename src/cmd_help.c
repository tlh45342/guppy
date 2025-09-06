// src/cmd_help.c

#include "cmd.h"

int cmd_help(int argc, char **argv) {
    print_all_commands();
    return 0;
}