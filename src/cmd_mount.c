// src/cmd_mount.c — mount a filesystem using the VFS router
// Usage:
//   mount -t <fstype> [-o key=val[,key=val]...] <device> <mountpoint>
// Behavior:
//   - No args: list current mounts via vfs_list_mounts()
//   - On success: VFS records the mount (no separate register call needed)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vblk.h"
#include "vfs.h"

static void usage(void) {
    fprintf(stderr,
        "usage: mount -t <fstype> [-o opts] <device> <mountpoint>\n"
        "  e.g.: mount -t fat -o ro /dev/a1 /mnt/a\n");
}

int cmd_mount(int argc, char **argv) {
    // "mount" with no args -> list mounts
    if (argc == 1) {
        vfs_list_mounts();
        return 0;
    }

    const char *fstype = NULL;
    const char *opts   = NULL;
    const char *device = NULL;
    const char *mntpt  = NULL;

    // Parse options
    int i = 1;
    for (; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "-t") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            fstype = argv[++i];
            continue;
        }
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            opts = argv[++i];
            continue;
        }
        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "mount: unknown option '%s'\n", a);
            return 1;
        }
        break;
    }

    // Positional args
    if (i < argc) device = argv[i++];
    if (i < argc) mntpt  = argv[i++];

    // Validate
    if (!fstype || !device || !mntpt || i != argc) {
        usage();
        return 1;
    }

    // Open the block device
    vblk_t *dev = vblk_open(device);
    if (!dev) {
        fprintf(stderr, "mount: cannot open device '%s'\n", device);
        return 1;
    }

    // Mount via the VFS (new signature: fstype, src(device), dev, mountpoint, opts)
    int rc = vfs_mount_dev(fstype, device, dev, mntpt, opts ? opts : "");
    if (rc != 0) {
        fprintf(stderr, "mount: failed to mount '%s' on '%s' as '%s'\n", device, mntpt, fstype);
        vblk_close(dev); // FS didn’t take ownership on failure
        return 1;
    }

    // Success — VFS already recorded the mount; FS owns 'dev' and will close it on umount
    return 0;
}
