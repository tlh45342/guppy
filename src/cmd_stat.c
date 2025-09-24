// local "stat" shim: show basic info about a host file
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>

#include "debug.h"
#ifndef DBG
#define DBG(...) do{}while(0)
#endif

static void usage(void) {
    printf("usage: stat <path>\n");
}

static const char* type_str(mode_t m) {
    if (S_ISREG(m))  return "regular file";
    if (S_ISDIR(m))  return "directory";
    if (S_ISLNK(m))  return "symlink";
    if (S_ISCHR(m))  return "char device";
    if (S_ISBLK(m))  return "block device";
    if (S_ISFIFO(m)) return "fifo";
    if (S_ISSOCK(m)) return "socket";
    return "unknown";
}

static void tm_to_str(time_t t, char out[32]) {
    struct tm tmv; localtime_r(&t, &tmv);
    strftime(out, 32, "%Y-%m-%d %H:%M:%S", &tmv);
}

int cmd_stat(int argc, char **argv) {
    if (argc != 2) { usage(); return 1; }
    const char *path = argv[1];

    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "stat: cannot stat '%s': %s\n", path, strerror(errno));
        return 1;
    }

    char mtime[32], atime[32], ctime_[32];
    tm_to_str(st.st_mtime, mtime);
    tm_to_str(st.st_atime, atime);
    tm_to_str(st.st_ctime, ctime_);

    printf("  File: %s\n", path);
    printf("  Type: %s\n", type_str(st.st_mode));
    printf("  Size: %" PRIuMAX " bytes\n", (uintmax_t)st.st_size);
    printf(" Links: %lu\n", (unsigned long)st.st_nlink);
    printf("  Mode: %o (octal)\n", (unsigned)(st.st_mode & 07777));
#ifdef __CYGWIN__
    printf("  UID:  %u  GID: %u\n", (unsigned)st.st_uid, (unsigned)st.st_gid);
#else
    printf("  UID:  %u  GID: %u\n", (unsigned)st.st_uid, (unsigned)st.st_gid);
#endif
    printf("Access: %s\n", atime);
    printf("Modify: %s\n", mtime);
    printf("Change: %s\n", ctime_);

#ifdef S_ISLNK
    if (S_ISLNK(st.st_mode)) {
        char linkto[4096];
        ssize_t n = readlink(path, linkto, sizeof(linkto)-1);
        if (n >= 0) { linkto[n] = '\0'; printf(" Target: %s\n", linkto); }
    }
#endif

    return 0;
}