#pragma once
#include <stdint.h>
#include "vblk.h"

typedef struct gendisk {
    char     name[32];
    uint32_t sector_size;
    uint64_t size_bytes;
} gendisk;

int disk_scan_partitions(struct gendisk *gd);
int add_disk(struct gendisk *gd);
int del_disk(const char *name);
int block_rescan(const char *devname);
