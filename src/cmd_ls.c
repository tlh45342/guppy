// src/cmd_ls.c

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cmds.h"
#include "cwd.h"
#include "mnttab.h"
#include "devmap.h"
#include "devutil.h"  // for showing device base if you want (optional)

static void ls_usage(void) {
    puts("ls [-l] [-a] [path]");
    puts("  Lists mounts at '/' for now. When a filesystem reader is added,");
    puts("  'ls' will enumerate real directory entries.");
}

static void ls_root_list(bool long_fmt, bool show_all) {
    // Optionally print . and .. (synthetic)
    if (show_all) {
        if (long_fmt) {
            printf("drwxr-xr-x  .\n");
            printf("drwxr-xr-x  ..\n");
        } else {
            printf(".  ..\n");
        }
    }

    // List mounts
    const int n = mnttab_count();
    if (n <= 0) return;

    if (long_fmt) {
        printf("%-12s %-10s %-6s %-8s %s\n", "Name", "Device", "Part", "FStype", "Image");
        printf("%-12s %-10s %-6s %-8s %s\n", "----", "------", "----", "------", "-----");
    }
    for (int i = 0; i < n; ++i) {
        const MountEntry *m = mnttab_get(i);
        // Only show entries whose mountpoint is "/" (root contents)
        if (strcmp(m->mpoint, "/") != 0) continue;

        const char *img = devmap_resolve(m->dev);
        if (long_fmt) {
            printf("%-12s %-10s %-6d %-8s %s\n",
                   "/", m->dev, m->part_index,
                   m->fstype[0] ? m->fstype : "-",
                   img ? img : "-");
        } else {
            // For short format, just show a single '/' marker (only one root mount)
            puts("/");
        }
    }
}

int cmd_ls(int argc, char **argv) {
    bool long_fmt = false, show_all = false;
    const char *path = NULL;

    // Parse flags and optional path
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                ls_usage();
                return 0;
            }
            // allow combined flags like -la
            bool ok = true;
            for (const char *p = argv[i] + 1; *p; ++p) {
                if (*p == 'l') long_fmt = true;
                else if (*p == 'a') show_all = true;
                else { ok = false; break; }
            }
            if (!ok) { ls_usage(); return 2; }
        } else {
            path = argv[i];
        }
    }
    if (!path) path = cwd_get();

    // Only root is meaningful right now (we'll add fs reading next)
    if (strcmp(path, "/") == 0) {
        ls_root_list(long_fmt, show_all);
        return 0;
    }

    // If the path is a mounted mountpoint, we can at least acknowledge it
    const MountEntry *m = mnttab_find_by_mpoint(path);
    if (m) {
        // Placeholder until the ext2 reader is ready
        fprintf(stderr, "ls: filesystem browsing not implemented yet for %s (fstype=%s)\n",
                path, m->fstype[0] ? m->fstype : "-");
        return 1;
    }

    // Not mounted, not root
    fprintf(stderr, "ls: no such directory (not mounted): %s\n", path);
    return 2;
}