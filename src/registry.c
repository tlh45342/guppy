// src/registry.c

#include "cmd.h"
#include <stddef.h>   // size_t, NULL
#include <stdio.h>    // printf
#include <ctype.h>    // tolower
#include <string.h>   // optional (not strictly required here)

/* --------------------------------------------------------------------------
   Portable case-insensitive strcmp (avoids _stricmp/strcasecmp differences)
   -------------------------------------------------------------------------- */
static int icmp(const char *s1, const char *s2) {
    unsigned char c1, c2;
    while (*s1 && *s2) {
        c1 = (unsigned char)tolower((unsigned char)*s1++);
        c2 = (unsigned char)tolower((unsigned char)*s2++);
        if (c1 != c2) return (int)c1 - (int)c2;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

/* --------------------------------------------------------------------------
   Command handlers (declare the ones you actually implement elsewhere)
   -------------------------------------------------------------------------- */
extern int cmd_create(int argc, char **argv);
extern int cmd_format(int argc, char **argv);
extern int cmd_part  (int argc, char **argv);
extern int cmd_mbr   (int argc, char **argv);
extern int cmd_do    (int argc, char **argv);
extern int cmd_help  (int argc, char **argv);
extern int cmd_exit  (int argc, char **argv);
extern int cmd_parted(int argc, char **argv);
extern int cmd_use   (int argc, char **argv);
extern int cmd_gpt   (int argc, char **argv);
extern int cmd_mount (int argc, char **argv);

/* --------------------------------------------------------------------------
   Registry
   -------------------------------------------------------------------------- */
   
static const Command g_cmds[] = {
    { "create", cmd_create, "create <img> --size 256MiB [--mbr|--gpt]" },
    { "mount",  cmd_mount,  "mount [-v] | mount -i <img> /dev/X [--ro] | mount -d /dev/X" },
    { "gpt",    cmd_gpt,    "gpt <init|add|print> <img|/dev/X> ..." },
    { "parted", cmd_parted, "parted -l <img|/dev/X>   # print partition table (MBR/GPT)" },
    { "mbr",    cmd_mbr,    "mbr print <img|/dev/X>" },
    { "part",   cmd_part,   "part add <img|/dev/X> --index N --type 0x0C --start 1MiB --size 32MiB" },
    { "format", cmd_format, "format <img|/dev/X> --fat32 --label NAME" },
    { "do",     cmd_do,     "do <scriptfile>   # run commands from file" },
    { "help",   cmd_help,   "help              # list commands" },
    { "exit",   cmd_exit,   "exit              # quit REPL" },
    { "quit",   cmd_exit,   "quit              # quit REPL" },  // alias
};

const Command *find_command(const char *name) {
    for (size_t i = 0; i < sizeof g_cmds / sizeof g_cmds[0]; ++i) {
        if (icmp(g_cmds[i].name, name) == 0) {
            return &g_cmds[i];
        }
    }
    return NULL;
}

void print_all_commands(void) {
	printf("DEBUG!\n");
    for (size_t i = 0; i < sizeof g_cmds / sizeof g_cmds[0]; ++i) {
        printf("  %-8s  %s\n", g_cmds[i].name, g_cmds[i].help);
    }
}
