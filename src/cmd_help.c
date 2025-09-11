// src/cmd_help.c

#include <stdio.h>

#include "cmds.h"

int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;  // silence unused warnings
	printf("DEBUG-x\n");
    print_all_commands();
    return 0;
}