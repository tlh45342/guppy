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
    puts("usage: ls [-l] [-a] [-h] [path]");
}

static void fmt_size(uint64_t size, int human, char *out, size_t cap) {
    static const char units[] = "BKMGTPE";
    double v = (double)size;
    unsigned u = 0;

    if (!human) {
        snprintf(out, cap, "%llu", (unsigned long long)size);
        return;
    }

    while (v >= 1024.0 && u + 1 < sizeof(units) - 1) {
        v /= 1024.0;
        ++u;
    }

    if (u == 0) snprintf(out, cap, "%llu", (unsigned long long)size);
    else if (v >= 10.0) snprintf(out, cap, "%.0f%c", v, units[u]);
    else snprintf(out, cap, "%.1f%c", v, units[u]);
}

// Turn st_mode into "drwxr-xr-x" style.
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
    int show_all = 0, longfmt = 0, human = 0;
    const char *path = ".";

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'a') show_all = 1;
                else if (*p == 'l') longfmt = 1;
                else if (*p == 'h') human = 1;
                else { print_usage(); return 1; }
            }
        } else {
            path = arg;
        }
    }

    struct file *df = NULL;
    if (vfs_open(path, 0, 0, &df) != 0) {
        fprintf(stderr, "ls: cannot open '%s'\n", path);
        return 1;
    }

    uint8_t buf[4096];

    for (;;) {
        ssize_t n = vfs_getdents64(df, buf, sizeof buf);
        if (n < 0) {
            vfs_close(df);
            fprintf(stderr, "ls: read error on '%s'\n", path);
            return 1;
        }
        if (n == 0) break;

        size_t off = 0;
        while (off < (size_t)n) {
            vfs_dirent64_t *de = (vfs_dirent64_t *)(buf + off);
            const char *name = de->d_name;

            if (!show_all && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) {
                off += de->d_reclen;
                continue;
            }

            if (!longfmt) {
                puts(name);
            } else {
                char full[PATH_MAX];
                if (strcmp(path, ".") == 0) snprintf(full, sizeof full, "%s", name);
                else if (strcmp(path, "/") == 0) snprintf(full, sizeof full, "/%s", name);
                else snprintf(full, sizeof full, "%s/%s", path, name);

                struct g_stat st;
                if (vfs_stat(full, &st) == 0) {
                    char modebuf[11];
                    char sizebuf[32];
                    char when[20];
                    const char *owner = "-";
                    const char *group = "-";
                    struct tm *tm;

                    fmt_mode(st.st_mode, modebuf);
                    fmt_size(st.st_size, human, sizebuf, sizeof sizebuf);

                    tm = localtime(&st.st_mtime);
                    if (tm) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tm);
                    else strcpy(when, "-");

                    printf("%s %2u %8s %8s %10s %s %s\n",
                           modebuf, 1u, owner, group,
                           sizebuf, when, name);
                } else {
                    char t = (de->d_type == VFS_DT_DIR) ? 'd'
                           : (de->d_type == VFS_DT_REG) ? '-'
                           : '?';
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
