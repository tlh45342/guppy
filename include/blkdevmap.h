#pragma once
#include "vblk.h"

// Look up an already-registered device by name (e.g., "/dev/a" or "/dev/a1").
// Returns NULL if not found.
vblk_t *blkdevmap_get(const char *name);

// Register (or replace) a device mapping.
// Returns 0 on success, -1 on failure.
int blkdevmap_add(const char *name, vblk_t *blk);

// Remove a device mapping (parent or child).
// Returns 0 on success, -1 if not found.
int blkdevmap_remove(const char *name);

// Optional: enumerate devices (for "use" command / listing).
typedef void (*blkdevmap_enum_fn)(const char *name, vblk_t *blk, void *arg);
void blkdevmap_foreach(blkdevmap_enum_fn fn, void *arg);
