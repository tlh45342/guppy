// src/cmd_registry.c

#include <stddef.h>   // size_t, NULL
#include <stdio.h>    // printf
#include <ctype.h>    // tolower

#include "cmds.h"

/* Local -> VFS copy command. */
int cmd_lcp(int argc, char **argv);
int cmd_chmod(int argc, char **argv);
int cmd_date(int argc, char **argv);

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
    { "cat",       cmd_cat,       "cat <path> [path...]" },
    { "cd",        cmd_cd,        "cd [path]  (cd / if omitted; supports .., ., and cd -)" },
    { "chmod",     cmd_chmod,     "chmod <octal-mode> <path>  # change VFS permission bits" },
    { "cp",        cmd_cp,        "cp <src> <dst>            # copy VFS file" },
    { "create",    cmd_create,    "create [-f raw|vdi] <img> <size> [--mbr]" },
    { "date",      cmd_date,      "date                      # print host local date/time" },
    { "debug",     cmd_debug,     "debug [iso|vfs|all] [on|off|toggle]" },
    { "do",        cmd_do,        "do <scriptfile>           # run commands from file" },
    { "echo",      cmd_echo,      "echo [-n] words... [ >|>> /path ]" },
    { "exit",      cmd_exit,      "exit                      # quit REPL" },
    { "format",    cmd_format,    "format <img|/dev/X> --fat32 --label NAME" },
    { "gpt",       cmd_gpt,       "gpt <init|add|print> <img|/dev/X> ..." },
    { "help",      cmd_help,      "help                      # list commands" },
    { "hexdump",   cmd_hexdump,   "hexdump [-n bytes] [-s offset] <path>" },
    { "lcat",      cmd_lcat,      "local cat" },
    { "lcp",       cmd_lcp,       "lcp <local-src> <vfs-dst> # copy host/local file into mounted VFS" },
    { "lls",       cmd_lls,       "local listing" },
    { "ls",        cmd_ls,        "ls [-l] [-a] [path]       # list files or mounts at '/'" },
    { "mbr",       cmd_mbr,       "mbr print <img|/dev/X>" },
    { "mkdir",     cmd_mkdir,     "mkdir <path>" },
    { "mkfs.ext2", cmd_mkfs_ext2, "mkfs.ext2 <dev> --part N [--label NAME]" },
    { "mkfs.fat",  cmd_mkfs_fat,  "Format /dev/* as FAT12/16/32" },
    { "mkfs.ntfs", cmd_mkfs_ntfs, "Write minimal NTFS boot sector (probe-only)" },
    { "mkfs_vfat", cmd_mkfs_vfat, "Format /dev/* as FAT (VFAT defaults)" },
    { "mount",     cmd_mount,     "mount [-t ext2|iso9660] <dev> <mp> [--part N]" },
    { "part",      cmd_part,      "part add <img|/dev/X> --index N --type 0x0C --start 1MiB --size 32MiB" },
    { "parted",    cmd_parted,    "parted -l <img|/dev/X>   # print partition table (MBR/GPT)" },
    { "partscan",  cmd_partscan,  "Scan GPT and register /dev/<base>N vblks" },
    { "pwd",       cmd_pwd,       "pwd                       # print current directory and backing mount" },
    { "quit",      cmd_exit,      "quit                      # quit REPL" },
    { "rm",        cmd_rm,        "rm <path> [path...]       # remove VFS regular file(s)" },
    { "sha256",    cmd_sha256,    "sha256 <path> [path...]   # SHA-256 of VFS file(s)" },
    { "stat",      cmd_stat,      "stat file/device" },
    { "use",       cmd_use,       "use -i <image> <dev> | use # map/list devices (/dev/a, /dev/b, ...)" },
    { "version",   cmd_version,   "displays version information" },
    { "write",     cmd_write,     "write <dev> <local-file> [offset] # raw host file -> device/partition" },
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
