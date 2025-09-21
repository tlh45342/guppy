// src/cmd_echo.c — echo text to stdout or to a file (creating parent dirs as needed)
// Usage:
//   echo [-n] [-a] [--] <text ...>                # print to stdout
//   echo [-n] [-a] [--] <text ...> <target_path>  # write to file (mkdir -p)
// Flags:
//   -n / --no-newline : do not append newline
//   -a / --append     : append to file instead of truncate

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "vfs.h"
#include "vfs_stat.h"

/* ---------- Helpers ---------- */

static bool path_exists_dir(const char *path) {
    struct g_stat st;
    if (vfs_stat(path, &st) != 0) return false;
    return VFS_S_ISDIR(st.st_mode);
}

/* Extract parent directory of path into out (returns true if there is a parent) */
static bool get_parent_dir(const char *path, char **out_parent) {
    *out_parent = NULL;
    if (!path || !*path) return false;

    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *sep = slash;
    if (bslash && (!sep || bslash > sep)) sep = bslash;

    if (!sep) return false;                 // no parent (current dir)
    size_t len = (size_t)(sep - path);
    if (len == 0) {                         // parent is root like "/"
        *out_parent = (char*)malloc(2);
        if (!*out_parent) return false;
        (*out_parent)[0] = '/'; (*out_parent)[1] = '\0';
        return true;
    }
    char *p = (char*)malloc(len + 1);
    if (!p) return false;
    memcpy(p, path, len);
    p[len] = '\0';
    *out_parent = p;
    return true;
}

/* Create a single directory if missing */
static bool mkdir_one_if_needed(const char *dir) {
    if (!dir || !*dir) return true;
    if (path_exists_dir(dir)) return true;
    return vfs_mkdir(dir, VFS_MODE_DIR_0755) == 0;
}

/* mkdir -p: create all components in 'dir' if missing */
static bool mkdir_p(const char *dir) {
    if (!dir || !*dir) return true;

    size_t len = strlen(dir);
    char *buf = (char*)malloc(len + 1);
    if (!buf) return false;
    memcpy(buf, dir, len + 1);

    /* Normalize backslashes to forward slashes for splitting */
    for (size_t i = 0; i < len; ++i) if (buf[i] == '\\') buf[i] = '/';

    /* Handle drive prefix on Windows, e.g., "C:/..." : keep "C:" intact */
    size_t start = 0;
    if (len >= 2 &&
        ((buf[0] >= 'A' && buf[0] <= 'Z') || (buf[0] >= 'a' && buf[0] <= 'z')) &&
        buf[1] == ':') {
        start = 2;
    }

    /* Skip leading '/' after drive or absolute root */
    if (start < len && buf[start] == '/') start++;

    /* Walk and create at each separator boundary */
    for (size_t i = start; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (!mkdir_one_if_needed(buf)) { free(buf); return false; }
            buf[i] = '/';
            /* skip consecutive slashes */
            while (i + 1 < len && buf[i + 1] == '/') i++;
        }
    }
    /* Create the final directory itself */
    bool ok = mkdir_one_if_needed(buf);
    free(buf);
    return ok;
}

/* Ensure parent directories for a target file exist (mkdir -p) */
static bool ensure_parent_dirs_for(const char *target) {
    char *parent = NULL;
    bool have_parent = get_parent_dir(target, &parent);
    if (!have_parent) return true;  // nothing to make
    bool ok = mkdir_p(parent);
    free(parent);
    return ok;
}

/* Write entire buffer to a file (append or truncate). Creates parents. */
static bool write_entire_file(const char *path, const void *data, size_t len, bool append) {
    if (!ensure_parent_dirs_for(path)) return false;

    int flags = VFS_O_WRONLY | VFS_O_CREAT |
                (append ? VFS_O_APPEND : VFS_O_TRUNC);
    struct file *f = NULL;
    if (vfs_open(path, flags, VFS_MODE_FILE_0644, &f) != 0 || !f) return false;

    const uint8_t *p = (const uint8_t*)data;
    size_t remain = len;
    while (remain > 0) {
        ssize_t w = vfs_write(f, p, remain);
        if (w < 0) { vfs_close(f); return false; }
        p += (size_t)w;
        remain -= (size_t)w;
    }
    vfs_close(f);
    return true;
}

/* Join argv[i..j] (inclusive) into a single string with spaces; caller frees. */
static char* join_words(char **argv, int i, int j, bool add_trailing_nl) {
    if (i > j) {
        char *s = (char*)malloc(add_trailing_nl ? 2 : 1);
        if (!s) return NULL;
        s[0] = '\0';
        if (add_trailing_nl) strcat(s, "\n");
        return s;
    }
    size_t total = 0;
    for (int k = i; k <= j; ++k) total += strlen(argv[k]) + 1; // +1 for space or NUL
    if (add_trailing_nl) total += 1;

    char *buf = (char*)malloc(total);
    if (!buf) return NULL;

    buf[0] = '\0';
    for (int k = i; k <= j; ++k) {
        strcat(buf, argv[k]);
        if (k != j) strcat(buf, " ");
    }
    if (add_trailing_nl) strcat(buf, "\n");
    return buf;
}

/* ---------- Command ---------- */

int cmd_echo(int argc, char **argv) {
    bool no_newline = false;
    bool append = false;

    /* Parse flags */
    int i = 1;
    for (; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "-n") == 0 || strcmp(a, "--no-newline") == 0) { no_newline = true; continue; }
        if (strcmp(a, "-a") == 0 || strcmp(a, "--append") == 0) { append = true; continue; }
        break;
    }

    int remaining = argc - i;
    if (remaining <= 0) {
        /* No text provided */
        if (!no_newline) {
#ifdef _WIN32
            _setmode(_fileno(stdout), _O_BINARY);
#endif
            fputs("\n", stdout);
        }
        return 0;
    }

    /* Heuristic: if ≥2 non-flag args, treat the last as target path */
    const char *target = NULL;
    int text_start = i, text_end = argc - 1;
    if (remaining >= 2) {
        target = argv[argc - 1];
        text_end = argc - 2;
    }

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    /* Build content buffer */
    char *content = join_words(argv, text_start, text_end, !no_newline);
    if (!content) { fprintf(stderr, "echo: out of memory\n"); return 1; }
    size_t content_len = strlen(content);

    if (!target) {
        /* Print to stdout */
        size_t w = fwrite(content, 1, content_len, stdout);
        free(content);
        return (w == content_len) ? 0 : 1;
    }

    /* Write to file (mkdir -p parents) */
    bool ok = write_entire_file(target, content, content_len, append);
    free(content);
    if (!ok) {
        fprintf(stderr, "echo: failed to write '%s'\n", target);
        return 1;
    }
    return 0;
}
