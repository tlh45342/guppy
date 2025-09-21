// vfs_init.c — call this during program init
#include "vfs.h"

// externs provided by drivers you build in:
extern const filesystem_type_t fat_fs_type;
extern const filesystem_type_t vfat_fs_type;
// extern const filesystem_type_t ext2_fs_type; etc.

void vfs_register_builtin_filesystems(void) {
    vfs_register(&fat_fs_type);
    vfs_register(&vfat_fs_type);
    // vfs_register(&ext2_fs_type); ...
}

/* Declare only the drivers you actually link */
extern const filesystem_type_t VFS_EXT2;
extern const filesystem_type_t VFS_FAT;
extern const filesystem_type_t VFS_VFAT;     /* optional */
extern const filesystem_type_t VFS_NTFS;     /* optional */
extern const filesystem_type_t VFS_ISO9660;  /* optional */

int vfs_register_builtin(void) {
    int ok = 0, saw_fat = 0, saw_vfat = 0;
    const filesystem_type_t *fat = NULL;

#ifdef VFS_EXT2
    if (vfs_register(&VFS_EXT2) == 0) ok++;
#endif
#ifdef VFS_FAT
    if (vfs_register(&VFS_FAT) == 0) { ok++; saw_fat = 1; fat = &VFS_FAT; }
#endif
#ifdef VFS_VFAT
    if (vfs_register(&VFS_VFAT) == 0) { ok++; saw_vfat = 1; }
#endif
#ifdef VFS_NTFS
    if (vfs_register(&VFS_NTFS) == 0) ok++;
#endif
#ifdef VFS_ISO9660
    if (vfs_register(&VFS_ISO9660) == 0) ok++;
#endif

    /* If there’s no separate VFAT, alias "vfat" → FAT */
    if (!saw_vfat && saw_fat && fat) (void)vfs_register_alias("vfat", fat);

    return ok ? 0 : -1;
}

/* Optional: single init you call early in main() */
int vfs_init(void) {
    return vfs_register_builtin();
}
