// include/cmd.h

#pragma once
#include <stdbool.h>

typedef struct {
    const char *name;                     // "create", "format", "part", "mbr", "do", "help", "exit"
    int  (*fn)(int argc, char **argv);    // handler
    const char *help;                     // short help line
} Command;

const Command *find_command(const char *name);
int run_command_line(const char *line);
void print_all_commands(void);
int cmd_parted(int argc, char **argv);

#define GUPPY_RC_EXIT 101  // special code meaning "please exit"

void guppy_request_exit(void);
int  guppy_exit_requested(void);
void guppy_clear_exit_request(void);

// commands
int cmd_create(int argc, char **argv);
int cmd_do(int argc, char **argv);
int cmd_exit(int argc, char **argv);
int cmd_format(int argc, char **argv);
int cmd_help(int argc, char **argv);
int cmd_mbr(int argc, char **argv);
int cmd_part(int argc, char **argv);
int cmd_parted(int argc, char **argv);
int cmd_gpt(int argc, char **argv);
int cmd_use(int argc, char **argv);
