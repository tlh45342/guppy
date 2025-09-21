// fs_vfat.c — Thin alias of FAT; diverge later for LFN/VFAT specifics.
#include <stddef.h>
#include <stdbool.h>
#include "vblk.h"
#include "vfs.h"

// Provided by your FAT driver module:
extern const filesystem_type_t fat_fs_type;

static bool vfat_probe(vblk_t *dev, char *label, size_t cap) {
    return fat_fs_type.probe ? fat_fs_type.probe(dev, label, cap) : false;
}
static int vfat_mount(vblk_t *dev, const char *opts, superblock_t **out) {
    return fat_fs_type.mount(dev, opts, out);
}
static void vfat_umount(superblock_t *sb) {
    if (fat_fs_type.umount) fat_fs_type.umount(sb);
}

const filesystem_type_t vfat_fs_type = {
    .name   = "vfat",
    .probe  = vfat_probe,
    .mount  = vfat_mount,
    .umount = vfat_umount,
};
