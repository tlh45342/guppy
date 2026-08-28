// src/cmd_stat.c — VFS-backed "stat" command
// usage: stat <path>
//
// IMPORTANT:
//   This command reports metadata for Guppy's mounted VFS namespace.
//   Host/local filesystem metadata belongs in local-side commands (lls, etc.).

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "vfs.h"
#include "vfs_stat.h"

static const char *type_str(uint32_t mode)
{
    if (VFS_S_ISREG(mode)) return "file";
    if (VFS_S_ISDIR(mode)) return "dir";
#ifdef VFS_S_ISCHR
    if (VFS_S_ISCHR(mode)) return "char";
#endif
#ifdef VFS_S_ISBLK
    if (VFS_S_ISBLK(mode)) return "block";
#endif
#ifdef VFS_S_ISFIFO
    if (VFS_S_ISFIFO(mode)) return "fifo";
#endif
#ifdef VFS_S_ISLNK
    if (VFS_S_ISLNK(mode)) return "symlink";
#endif
#ifdef VFS_S_ISSOCK
    if (VFS_S_ISSOCK(mode)) return "socket";
#endif
    return "unknown";
}

static void time_to_str(int64_t sec, char *buf, size_t bufsz)
{
    time_t tt = (time_t)sec;
    struct tm *tmv = localtime(&tt);

    if (!tmv) {
        snprintf(buf, bufsz, "%" PRId64, sec);
        return;
    }

    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", tmv);
}

int cmd_stat(int argc, char **argv)
{
    struct g_stat st;
    const char *path;
    char mt[32], at[32], ct[32];

    if (argc < 2) {
        fprintf(stderr, "usage: stat <path>\n");
        return 1;
    }

    path = argv[1];
    memset(&st, 0, sizeof st);

    if (vfs_stat(path, &st) != 0) {
        fprintf(stderr, "stat: cannot stat '%s'\n", path);
        return 1;
    }

    time_to_str(st.st_mtim.tv_sec, mt, sizeof mt);
    time_to_str(st.st_atim.tv_sec, at, sizeof at);
    time_to_str(st.st_ctim.tv_sec, ct, sizeof ct);

    printf("Path: %s\n", path);
    printf("Type: %s\n", type_str(st.st_mode));
    printf("Mode: %o\n", (unsigned)(st.st_mode & 07777u));
    printf("Size: %" PRIu64 "\n", st.st_size);
    printf("Links: %" PRIu32 "\n", st.st_nlink);
    printf("UID: %" PRIu32 "\n", st.st_uid);
    printf("GID: %" PRIu32 "\n", st.st_gid);
    printf("Inode: %" PRIu64 "\n", st.st_ino);
    printf("Blocks: %" PRIu64 "\n", st.st_blocks);
    printf("Block size: %" PRIu32 "\n", st.st_blksize);
    printf("Access: %s\n", at);
    printf("Modify: %s\n", mt);
    printf("Change: %s\n", ct);

    return 0;
}
