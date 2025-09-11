#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAX_DEVICES 16
#define MAX_MOUNTS  16

typedef struct {
    char dev[32];   // "/dev/a"
    char path[260]; // "image.img"
} DeviceMap;

typedef struct {
    char dev[32];      // "/dev/a"
    int  part_index;   // 0 = whole device, 1..N = partition #
    char fstype[16];   // "ext2" (optional hint; can stay "")
    char mpoint[64];   // "/"
} MountEntry;

bool registry_add_device(const char *dev, const char *path);
const char* registry_resolve_path(const char *dev); // returns path or NULL
void registry_list_devices(void);

bool registry_add_mount(const char *dev, int part_index, const char *fstype, const char *mpoint);
void registry_list_mounts(void);
const MountEntry* registry_find_mount(const char *mpoint);
