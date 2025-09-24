#include "vfs.h"

static bool ntfs_probe(vblk_t *dev, char *label, size_t cap) {
    (void)dev; if (label && cap) label[0] = 0; return false;
}
static int ntfs_mount(vblk_t *dev, const char *opts, superblock_t **out) {
    (void)dev; (void)opts; (void)out; return -1;
}
const filesystem_type_t VFS_NTFS = {
    .name  = "ntfs",
    .probe = ntfs_probe,
    .mount = ntfs_mount,
    .umount= NULL,
};