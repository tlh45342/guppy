// src/cmd_ls.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "debug.h"
#include "vfs.h"
#include "gu_dirent.h"   // provides DIR, vfs_opendir/vfs_readdir/vfs_closedir
#include "vfs_stat.h"    // struct g_stat, vfs_stat()

// flags
#define LS_FLAG_ALL   0x01  // -a
#define LS_FLAG_LONG  0x02  // -l

// portable type chars (prefer dirent d_type; fallback to mode if we had it)
static char type_char_from_dtype(uint8_t dt) {
    switch (dt) {
        case VFS_DT_DIR: return 'd';
        case VFS_DT_LNK: return 'l';
        case VFS_DT_REG: return '-';
        default:         return '?';
    }
}

static void join_path(char *out, size_t cap, const char *base, const char *name) {
    if (!base || base[0] == '\0') { snprintf(out, cap, "%s", name); return; }
    size_t bl = strlen(base);
    if (bl > 0 && base[bl-1] == '/')
        snprintf(out, cap, "%s%s", base, name);
    else if (strcmp(base, "/") == 0)
        snprintf(out, cap, "/%s", name);
    else
        snprintf(out, cap, "%s/%s", base, name);
    out[cap-1] = '\0';
}

static int list_dir(const char *path, unsigned flags) {
    DIR *d = vfs_opendir(path && *path ? path : ".");
    if (!d) {
        // Fallback: maybe it's a file, not a dir
        struct g_stat st;
        if (vfs_stat(path ? path : ".", &st) == 0) {
            // Print just the file itself (minimal info)
            if (flags & LS_FLAG_LONG) {
                unsigned long long sz = (unsigned long long)st.st_size;
                printf("%10llu %s\n", sz, path ? path : ".");
            } else {
                printf("%s\n", path ? path : ".");
            }
            return 0;
        }
        fprintf(stderr, "ls: cannot access '%s'\n", path ? path : ".");
        return 1;
    }

    // Directory listing
    const bool show_all = (flags & LS_FLAG_ALL) != 0;
    const bool longfmt  = (flags & LS_FLAG_LONG) != 0;

    for (;;) {
        struct dirent *de = vfs_readdir(d);
        if (!de) break;

        const char *name = de->d_name;
        if (!show_all) {
            // skip dot entries and hidden names by default
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            if (name[0] == '.') continue;
        }

        if (longfmt) {
            // try to stat each entry for size; if it fails, print 0
            char full[1024];
            join_path(full, sizeof full, path && *path ? path : ".", name);
            struct g_stat st;
            unsigned long long sz = 0ULL;
            if (vfs_stat(full, &st) == 0) sz = (unsigned long long)st.st_size;

            char t = type_char_from_dtype(de->d_type);
            printf("%c %10llu %s\n", t, sz, name);
        } else {
            printf("%s\n", name);
        }
    }

    vfs_closedir(d);
    return 0;
}

static int parse_flags(int argc, char **argv, unsigned *out_flags, int *first_path_idx) {
    unsigned f = 0;
    int i = 1;
    for (; i < argc; ++i) {
        const char *a = argv[i];
        if (!a || a[0] != '-' || a[1] == '\0') break; // not an option (or "-" alone)
        // short options, combined allowed: -la → -l -a
        for (int j = 1; a[j]; ++j) {
            switch (a[j]) {
                case 'a': f |= LS_FLAG_ALL;  break;
                case 'l': f |= LS_FLAG_LONG; break;
                case '-': // stop option parsing on "--"
                    if (a[j+1] == '\0') { i++; goto done; }
                    // treat as non-option if something like "-x--y" (unlikely)
                    break;
                default:
                    fprintf(stderr, "ls: unknown option '-%c'\n", a[j]);
                    return -1;
            }
        }
    }
done:
    *out_flags = f;
    *first_path_idx = i;
    return 0;
}

int cmd_ls(int argc, char **argv) {
    unsigned flags = 0;
    int path_i = 1;
    if (parse_flags(argc, argv, &flags, &path_i) != 0) {
        fprintf(stderr, "usage: ls [-l] [-a] [path...]\n");
        return 1;
    }

    int rc = 0;

    // If no paths, use "."
    if (path_i >= argc) {
        DBG("ls: no path args; using \".\"");
        return list_dir(".", flags);
    }

    // If multiple paths, print headers
    int n_paths = argc - path_i;
    for (int k = path_i; k < argc; ++k) {
        const char *p = argv[k];
        if (n_paths > 1) {
            if (k > path_i) putchar('\n');
            printf("%s:\n", p);
        }
        int r = list_dir(p, flags);
        if (r != 0) rc = r;
    }
    return rc;
}
