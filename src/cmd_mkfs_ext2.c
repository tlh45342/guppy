#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "vblk.h"
#include "ext2.h"

static void usage(void) { printf("mkfs.ext2 <device> [--label NAME]\n"); }

int cmd_mkfs_ext2(int argc, char **argv)
{
    if (argc < 2) { usage(); return 0; }
    const char *target = argv[1];
    const char *label = NULL;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--label") == 0 && i + 1 < argc) label = argv[++i];
        else { fprintf(stderr, "mkfs.ext2: unknown option '%s'\n", argv[i]); return 0; }
    }

    vblk_t *dev = vblk_open(target);
    if (!dev) {
        fprintf(stderr, "mkfs.ext2: unknown device %s (use -i <img> %s first)\n", target, target);
        return 0;
    }
    uint64_t len = vblk_size_bytes(dev);
    if (!len) { fprintf(stderr, "mkfs.ext2: cannot determine size for %s\n", target); return 0; }

    printf("mkfs.ext2: formatting %s (size=%" PRIu64 " bytes)%s%s\n",
           target, len, label ? " label=" : "", label ? label : "");
    int rc = mkfs_ext2_core(dev, len, label ? label : "");
    if (rc != 0) { fprintf(stderr, "mkfs.ext2: failed (rc=%d)\n", rc); return 0; }
    if (!vblk_flush(dev)) { fprintf(stderr, "mkfs.ext2: flush failed\n"); return 0; }
    printf("mkfs.ext2: done\n");
    return 0;
}
