#include "cmd.h"
#include <stdio.h>
#include <string.h>

// split a line into argv[], handle quotes "..." and flags --x=y
int parse_argv(char *line, int maxv, char **argv);  // put this in parse.c

int run_command_line(char *line) {
    char *argv[32]; int argc = parse_argv(line, 32, argv);
    if (argc == 0) return 0;
    const Command *cmd = find_command(argv[0]);
    if (!cmd) { printf("Unknown command. Try 'help'.\n"); return 2; }
    return cmd->fn(argc, argv);
}
