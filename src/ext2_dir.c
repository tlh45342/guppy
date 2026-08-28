/*
 * ext2_dir.c
 *
 * Persistent directory creation now lives in ext2.c as ext2_mkdir_at(),
 * because mutation must be tied to a specific backing image/partition.
 * This translation unit is intentionally kept as a placeholder so existing
 * source-tree/build expectations do not need to change.
 */
#include "ext2.h"
