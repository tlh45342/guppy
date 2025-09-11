#pragma once
#include <stdbool.h>
#include <stdint.h>

// Check for ext2/3/4 magic (0xEF53) at superblock offset.
// fs_offset_bytes = byte offset where the filesystem starts.
bool probe_ext2_magic(const char *image_path, uint64_t fs_offset_bytes);