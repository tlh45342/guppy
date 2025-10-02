// src/cmd_ls.c — list directory via VFS getdents64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "vfs.h"
#include "vfs_stat.h"
#include "debug.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static void print_usage(void) {
    puts("usage: ls [-l] [-a] [path]");
}

// Turn st_mode into "drwxr-xr-x" style.
// Uses VFS_S_ISDIR for the file type; rwx bits use POSIX-style masks.
static void fmt_mode(uint32_t mode, char out[11]) {
    out[0] = VFS_S_ISDIR(mode) ? 'd' : '-';
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 0040) ? 'r' : '-';
    out[5] = (mode & 0020) ? 'w' : '-';
    out[6] = (mode & 0010) ? 'x' : '-';
    out[7] = (mode & 0004) ? 'r' : '-';
    out[8] = (mode & 0002) ? 'w' : '-';
    out[9] = (mode & 0001) ? 'x' : '-';
    out[10] = '\0';
}

int cmd_ls(int argc, char **argv)
{
    int show_all = 0, longfmt = 0;
    const char *path = ".";

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'a') show_all = 1;
                else if (*p == 'l') longfmt = 1;
                else { print_usage(); return 1; }
            }
        } else {
            path = arg;
        }
    }

    // Open the target path (directory)
    struct file *df = NULL;
    if (vfs_open(path, 0, 0, &df) != 0) {
        fprintf(stderr, "ls: cannot open '%s'\n", path);
        return 1;
    }

    // Iterate directory entries using getdents64
    uint8_t buf[4096];

    for (;;) {
        ssize_t n = vfs_getdents64(df, buf, sizeof buf);
        if (n < 0) {
            vfs_close(df);
            fprintf(stderr, "ls: read error on '%s'\n", path);
            return 1;
        }
        if (n == 0) break; // EOF

        size_t off = 0;
        while (off < (size_t)n) {
            vfs_dirent64_t *de = (vfs_dirent64_t *)(buf + off);
            const char *name = de->d_name;

            // skip dot entries unless -a
            if (!show_all && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) {
                off += de->d_reclen;
                continue;
            }

            if (!longfmt) {
                puts(name);
            } else {
                // long listing: perms, nlink, owner, group, size, name
                char full[PATH_MAX];
                if (strcmp(path, ".") == 0) snprintf(full, sizeof full, "%s", name);
                else                         snprintf(full, sizeof full, "%s/%s", path, name);

                struct g_stat st;
                if (vfs_stat(full, &st) == 0) {
                    char modebuf[11];
                    fmt_mode(st.st_mode, modebuf);

                    // placeholders for owner/group (we'll wire real values later)
                    const char *owner = "-";
                    const char *group = "-";
					char when[20];
					    struct tm *tm = localtime(&st.st_mtime);
					    if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
					    else    strcpy(when, "-");
					
					    printf("%s %2u %8s %8s %10llu %s %s\n",
					           modebuf, 1u, owner, group,
					           (unsigned long long)st.st_size,
					           when,
					           name);
                } else {
                    // stat failed: use dirent type as fallback for leading char
                    char t = (de->d_type == VFS_DT_DIR) ? 'd'
                           : (de->d_type == VFS_DT_REG) ? '-'
                           : '?';
                    // perms unknown → "---------" placeholder
                    printf("%c%9s %2u %8s %8s %10s %s\n",
                           t, "---------", 1u, "-", "-", "-", name);
                }
            }

            off += de->d_reclen;
        }
    }

    vfs_close(df);
    return 0;
}
