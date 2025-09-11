// src/cmd_registry.c

#include <stddef.h>   // size_t, NULL
#include <stdio.h>    // printf
#include <ctype.h>    // tolower

#include "cmds.h"

/* --------------------------------------------------------------------------
   Portable case-insensitive strcmp (avoids _stricmp/strcasecmp differences)
   -------------------------------------------------------------------------- */
static int icmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        unsigned char c1 = (unsigned char)tolower((unsigned char)*s1++);
        unsigned char c2 = (unsigned char)tolower((unsigned char)*s2++);
        if (c1 != c2) return (int)c1 - (int)c2;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

/* --------------------------------------------------------------------------
   Command registry
   -------------------------------------------------------------------------- */

static const Command g_cmds[] = {
    { "create",    cmd_create,    "create <img> --size 256MiB [--mbr|--gpt]" },
    { "gpt",       cmd_gpt,       "gpt <init|add|print> <img|/dev/X> ..." },
    { "parted",    cmd_parted,    "parted -l <img|/dev/X>   # print partition table (MBR/GPT)" },
    { "mbr",       cmd_mbr,       "mbr print <img|/dev/X>" },
    { "pwd",       cmd_pwd,       "pwd                       # print current directory and backing mount" },
    { "ls",        cmd_ls,        "ls [-l] [-a] [path]       # list files or mounts at '/'" },
    { "part",      cmd_part,      "part add <img|/dev/X> --index N --type 0x0C --start 1MiB --size 32MiB" },
    { "format",    cmd_format,    "format <img|/dev/X> --fat32 --label NAME" },
	{ "mkdir", cmd_mkdir, "mkdir <path>" }, 
    { "mkfs.ext2", cmd_mkfs_ext2, "mkfs.ext2 <dev> --part N [--label NAME]" },
	{ "cd",    cmd_cd,    "cd [path]  (cd / if omitted; supports .., ., and cd -)" },
    { "mount",     cmd_mount,     "mount [-t ext2|iso9660] <dev> <mp> [--part N]" },
    { "cp",        cmd_cp,        "cp <src> <dst>            # copy (ISO -> ext2 root for now)" },
    { "use",       cmd_use,       "use -i <image> <dev> | use # map/list devices (/dev/a, /dev/b, ...)" },
    { "do",        cmd_do,        "do <scriptfile>           # run commands from file" },
    { "help",      cmd_help,      "help                      # list commands" },
    { "exit",      cmd_exit,      "exit                      # quit REPL" },
    { "quit",      cmd_exit,      "quit                      # quit REPL" },  // alias
};

static size_t cmd_count(void) { return sizeof g_cmds / sizeof g_cmds[0]; }

const Command *find_command(const char *name) {
    for (size_t i = 0; i < cmd_count(); ++i) {
        if (icmp(g_cmds[i].name, name) == 0) return &g_cmds[i];
    }
    return NULL;
}

void print_all_commands(void) {
    for (size_t i = 0; i < cmd_count(); ++i) {
        printf("  %-10s %s\n", g_cmds[i].name, g_cmds[i].help);
    }
}
