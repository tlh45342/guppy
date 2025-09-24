// src/cmd_lls.c — local "ls" for current working directory (or a given path)
// supports: lls [-l] [-a] [path]

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "debug.h"
#ifndef DBG
#define DBG(...) do{}while(0)
#endif

static void usage(void) {
    printf("usage: lls [-l] [-a] [path]\n");
}

static void mode_to_str(mode_t m, char out[11]) {
    out[0]  = S_ISDIR(m) ? 'd' :
              S_ISLNK(m) ? 'l' :
              S_ISCHR(m) ? 'c' :
              S_ISBLK(m) ? 'b' :
              S_ISFIFO(m)? 'p' :
              S_ISSOCK(m)? 's' : '-';
    out[1]  = (m & S_IRUSR) ? 'r' : '-';
    out[2]  = (m & S_IWUSR) ? 'w' : '-';
    out[3]  = (m & S_IXUSR) ? 'x' : '-';
    out[4]  = (m & S_IRGRP) ? 'r' : '-';
    out[5]  = (m & S_IWGRP) ? 'w' : '-';
    out[6]  = (m & S_IXGRP) ? 'x' : '-';
    out[7]  = (m & S_IROTH) ? 'r' : '-';
    out[8]  = (m & S_IWOTH) ? 'w' : '-';
    out[9]  = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static void print_long(const char *dir, const char *name) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return;

    struct stat st;
    if (lstat(path, &st) != 0) {
        printf("?????????? ? %12s %s (lstat: %s)\n", "?", name, strerror(errno));
        return;
    }

    char perm[11]; mode_to_str(st.st_mode, perm);

    char tbuf[32];
    struct tm tmv;
    time_t tt = st.st_mtime;
#if defined(_POSIX_C_SOURCE)
    localtime_r(&tt, &tmv);
#else
    tmv = *localtime(&tt);
#endif
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tmv);

    // symlink target (best-effort)
    char linkto[4096]; ssize_t ln = 0;
#if defined(S_ISLNK) && !defined(_WIN32)
    if (S_ISLNK(st.st_mode)) {
        ln = readlink(path, linkto, sizeof(linkto)-1);
        if (ln >= 0) { linkto[ln] = '\0'; }
    }
#endif

    printf("%s %3lu %10" PRIuMAX " %s %s",
           perm,
           (unsigned long)st.st_nlink,
           (uintmax_t)st.st_size,
           tbuf,
           name);
    if (ln > 0) printf(" -> %s", linkto);
    printf("\n");
}

int cmd_lls(int argc, char **argv) {
    int opt_long = 0, opt_all = 0;
    const char *path = ".";

    // parse flags: -l, -a (allow -la/-al)
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            for (const char *p = a+1; *p; ++p) {
                if (*p == 'l') opt_long = 1;
                else if (*p == 'a') opt_all = 1;
                else { usage(); return 1; }
            }
        } else {
            path = a;
        }
    }

    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "lls: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (!opt_all && name[0] == '.') continue; // skip dot files unless -a

        if (opt_long) print_long(path, name);
        else printf("%s\n", name);
    }

    closedir(d);
    return 0;
}
