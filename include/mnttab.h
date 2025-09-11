#pragma once
#include <stdbool.h>

typedef struct {
    char dev[32];     // "/dev/a"
    int  part_index;  // 0 = whole device; 1..N = partition
    char fstype[16];  // "ext2"
    char mpoint[64];  // "/"
} MountEntry;

bool mnttab_add(const char *dev, int part_index, const char *fstype, const char *mpoint);
void mnttab_list(void);
const MountEntry* mnttab_find_by_mpoint(const char *mpoint);

// NEW: lightweight iteration API for nicer "mount" listings
int  mnttab_count(void);
const MountEntry* mnttab_get(int index);
