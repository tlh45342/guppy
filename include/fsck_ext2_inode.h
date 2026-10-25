#ifndef GUPPY_FSCK_EXT2_INODE_H
#define GUPPY_FSCK_EXT2_INODE_H
#include <stdint.h>
#include "vblk.h"
#include "fsck_ext2_blocks.h"
/* Read-only inode table validation. Returns number of errors, or -1 on I/O failure. */
int fsck_ext2_check_inodes(vblk_t *dev, uint32_t group, uint32_t inodes_per_group,
                           uint32_t count, uint32_t inode_size, uint32_t block_size,
                           uint32_t table_block, const uint8_t *bitmap,
                           uint32_t first_nonreserved, fsck_block_context *blocks);
#endif
