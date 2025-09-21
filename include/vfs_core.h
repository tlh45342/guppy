#pragma once
#include "vblk.h"
#include "vfs.h"

/* Compatibility alias: older code used vfs_driver_t */
typedef filesystem_type_t vfs_driver_t;

/* Statically defined registry (provided by vfs_core.c) */
extern const vfs_driver_t *const vfs_registry[];
extern const int vfs_registry_count;

/* Try each driver’s .probe() and return the first that claims the device, or NULL. */
const vfs_driver_t *vfs_probe_any(vblk_t *dev);

/* Register all drivers with the VFS router.
   Also installs an alias "vfat" → "fat" if "vfat" is not present but "fat" is. */
int vfs_register_builtin(void);
