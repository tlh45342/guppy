// include/disk.h

#pragma once
#include <stdint.h>

// Default logical sector size for plain image files
#define GUPPY_DEFAULT_SECTOR 512u

static inline int is_sector_aligned(uint64_t v, uint32_t sec) {
    return (v % sec) == 0;
}

typedef struct GuppyImage GuppyImage;
GuppyImage* guppy_create_image(const char* path, uint64_t size_bytes, bool sparse);
int guppy_close_image(GuppyImage* img);