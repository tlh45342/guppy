// src/cmd_mkdir.c — minimal mkdir on top of the VFS
// Usage:
//   mkdir [-p] <dir> [<dir> ...]
//
// -p : create parent directories as needed (mkdir -p)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vfs.h"
#include "vfs_stat.h"  // for struct g_stat and VFS_S_ISDIR

/* --- helpers ----------------------------------------------------------- */

static bool path_exists_dir(const char *path) {
    struct g_stat st;
    if (vfs_stat(path, &st) != 0) return false;
    return VFS_S_ISDIR(st.st_mode);
}

static bool mkdir_one_if_needed(const char *dir) {
    if (!dir || !*dir) return true;
    if (path_exists_dir(dir)) return true;               // already there
    return vfs_mkdir(dir, VFS_MODE_DIR_0755) == 0;       // create one level
}

/* mkdir -p */
static bool mkdir_p(const char *dir) {
    if (!dir || !*dir) return true;

    size_t len = strlen(dir);
    char *buf = (char*)malloc(len + 1);
    if (!buf) return false;
    memcpy(buf, dir, len + 1);

    // Normalize backslashes to forward slashes
    for (size_t i = 0; i < len; ++i) if (buf[i] == '\\') buf[i] = '/';

    // Handle drive prefix on Windows (e.g., "C:/...")
    size_t start = 0;
    if (len >= 2 &&
        ((buf[0] >= 'A' && buf[0] <= 'Z') || (buf[0] >= 'a' && buf[0] <= 'z')) &&
        buf[1] == ':') {
        start = 2;
    }

    // Skip leading '/'
    while (start < len && buf[start] == '/') start++;

    // Walk and create each component
    for (size_t i = start; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (!mkdir_one_if_needed(buf)) { free(buf); return false; }
            buf[i] = '/';
            while (i + 1 < len && buf[i + 1] == '/') i++;  // collapse multiple '/'
        }
    }

    // Create final component
    bool ok = mkdir_one_if_needed(buf);
    free(buf);
    return ok;
}

/* --- command ----------------------------------------------------------- */

int cmd_mkdir(int argc, char **argv) {
    bool pflag = false;
    int i = 1;

    // parse flags
    for (; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "-p") == 0) { pflag = true; continue; }
        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "mkdir: unknown option '%s'\n", a);
            return 1;
        }
        break;
    }

    if (i >= argc) {
        fprintf(stderr, "usage: mkdir [-p] <dir> [<dir> ...]\n");
        return 1;
    }

    int rc = 0;
    for (; i < argc; ++i) {
        const char *path = argv[i];
        bool ok = pflag ? mkdir_p(path) : (vfs_mkdir(path, VFS_MODE_DIR_0755) == 0);
        if (!ok) {
            fprintf(stderr, "mkdir: cannot create directory '%s'\n", path);
            rc = 1;
        }
    }
    return rc;
}
