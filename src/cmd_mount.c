// src/cmd_mount.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vblk.h"
#include "vfs.h"
#include "debug.h"
#ifndef DBG
#define DBG(...) do{}while(0)
#endif

// -----------------------------------------------------------------------------
// Usage
// -----------------------------------------------------------------------------
static void usage(void) {
    printf(
        "usage: mount [-t <fstype>] [-o opts] <device> <mountpoint>\n"
        "  e.g.: mount -t fat -o ro /dev/a1 /mnt/a\n"
        "        mount /dev/b /mnt/iso        # auto-probe filesystem\n"
    );
}

// -----------------------------------------------------------------------------
// Auto-probe helpers (standard C; no nested functions)
// -----------------------------------------------------------------------------
typedef struct {
    vblk_t *dev;
    const filesystem_type_t *found;
} ap_ctx_t;

static int autoprobe_cb(const filesystem_type_t *fs, void *user) {
    ap_ctx_t *ctx = (ap_ctx_t*)user;
    if (fs && fs->probe && fs->probe(ctx->dev, NULL, 0)) {
        ctx->found = fs;
        return 1; // stop iteration
    }
    return 0;     // continue
}

static const filesystem_type_t* autoprobe_fs(vblk_t *dev) {
    ap_ctx_t ctx = { dev, NULL };
    vfs_for_each_fs(autoprobe_cb, &ctx);
    return ctx.found;
}

// -----------------------------------------------------------------------------
// Command
// -----------------------------------------------------------------------------
int cmd_mount(int argc, char **argv) {
    const char *fstype = NULL;
    const char *opts   = NULL;
    const char *device = NULL;
    const char *mntpt  = NULL;

    // Parse args
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        } else if (!strcmp(a, "-t")) {
            if (i + 1 >= argc) { usage(); return 1; }
            fstype = argv[++i];
        } else if (!strcmp(a, "-o")) {
            if (i + 1 >= argc) { usage(); return 1; }
            opts = argv[++i];
        } else if (!device) {
            device = a;
        } else if (!mntpt) {
            mntpt = a;
        } else {
            usage();
            return 1;
        }
    }

    // Validate (-t is optional now)
    if (!device || !mntpt) {
        usage();
        return 1;
    }

    // Open device (retry without /dev/ prefix if needed)
    vblk_t *dev = vblk_open(device);
    if (!dev && strncmp(device, "/dev/", 5) == 0) {
        DBG("mount: vblk_open('%s') failed; retrying without /dev/ prefix", device);
        dev = vblk_open(device + 5);
    }
    if (!dev) {
        fprintf(stderr, "mount: cannot open device '%s'\n", device);
        return 1;
    }

    // Auto-probe filesystem if not provided
    if (!fstype) {
        const filesystem_type_t *fs = autoprobe_fs(dev);
        if (!fs) {
            fprintf(stderr, "mount: could not detect filesystem on '%s'\n", device);
            vblk_close(dev);
            return 1;
        }
        fstype = fs->name; // use detected name
    }

    // Mount via VFS
    int rc = vfs_mount_dev(fstype, device, dev, mntpt, opts ? opts : "");
    if (rc != 0) {
        fprintf(stderr, "mount: failed to mount '%s' on '%s' as '%s'\n", device, mntpt, fstype);
        // On failure, FS did not adopt the handle; close it here.
        vblk_close(dev);
        return 1;
    }

    // Success: FS owns 'dev' and VFS recorded the mount.
    return 0;
}
