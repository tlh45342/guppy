#include "vfs.h"

static bool vfat_probe(vblk_t *dev, char *label, size_t cap) {
    (void)dev; if (label && cap) label[0] = 0; return false;  // no positive probe yet
}
static int vfat_mount(vblk_t *dev, const char *opts, superblock_t **out) {
    (void)dev; (void)opts; (void)out; return -1;              // not mountable yet
}
const filesystem_type_t VFS_VFAT = {
    .name  = "vfat",
    .probe = vfat_probe,
    .mount = vfat_mount,
    .umount= NULL,
};