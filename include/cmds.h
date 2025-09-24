// include/cmd.h

#pragma once
#include <stdbool.h>

typedef struct {
    const char *name;                     // "create", "format", "part", "mbr", "do", "help", "exit"
    int  (*fn)(int argc, char **argv);    // handler
    const char *help;                     // short help line
} Command;

struct vblk;

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
int cmd_pwd(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_mount(int argc, char **argv);
int cmd_mkfs_ext2(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_cat(int argc, char **argv);
int cmd_debug(int, char**);
int cmd_echo(int argc, char **argv);
int cmd_mkfs_fat(int argc, char **argv);
int cmd_mkfs_vfat(int argc, char **argv);
int cmd_mkfs_ntfs(int argc, char **argv);
int cmd_partscan(int argc, char **argv);
int cmd_version(int argc, char **argv);
int cmd_lls(int argc, char **argv);
int cmd_lcat(int argc, char **argv);
int cmd_stat(int argc, char **argv);