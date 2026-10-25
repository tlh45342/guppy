#ifndef FSCK_EXT2_BLOCKS_H
#define FSCK_EXT2_BLOCKS_H
#include <stdint.h>
#include "vblk.h"
typedef struct fsck_block_context {
 uint32_t count;
 uint32_t block_size;
 vblk_t *dev;
 uint8_t *allocated;
 uint32_t *owners;
 uint32_t inode_count;
 uint8_t *inode_allocated;
 uint32_t *directory_refs;
 uint16_t *inode_links;
 uint8_t *inode_checked;
} fsck_block_context;
int fsck_check_direct(fsck_block_context *ctx, uint32_t ino, const uint8_t *inode, uint16_t type);
int fsck_check_directory(fsck_block_context *ctx, uint32_t ino, const uint8_t *inode);
#endif
