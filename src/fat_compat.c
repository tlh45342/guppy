// src/fat_compat.c — provide legacy symbol `fat_fs_type` by forwarding to VFS_FAT
#include <stdbool.h>
#include "vfs.h"
#include "vblk.h"

extern const filesystem_type_t VFS_FAT;

/* Shims so we can use function addresses (valid in global initializers) */
static bool fat_probe_shim(vblk_t *dev, char *label_out, size_t label_cap) {
    return VFS_FAT.probe ? VFS_FAT.probe(dev, label_out, label_cap) : false;
}
static int fat_mount_shim(vblk_t *dev, const char *opts, superblock_t **out_sb) {
    return VFS_FAT.mount ? VFS_FAT.mount(dev, opts, out_sb) : -1;
}
static void fat_umount_shim(superblock_t *sb) {
    if (VFS_FAT.umount) VFS_FAT.umount(sb);
}

/* Legacy symbol expected by older code */
const filesystem_type_t fat_fs_type = {
    .name   = "fat",
    .probe  = fat_probe_shim,
    .mount  = fat_mount_shim,
    .umount = fat_umount_shim,
};