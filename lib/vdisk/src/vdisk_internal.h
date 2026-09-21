#pragma once
#include "vdisk.h"
#include <stdio.h>

typedef struct vdisk_driver vdisk_driver_t;

struct vdisk {
    const vdisk_driver_t *driver;
    FILE *fp;
    char *path;
    uint64_t size_bytes;      /* logical guest disk size */
    uint64_t data_offset;     /* fixed-image guest byte 0 in host file */
    uint64_t host_size_bytes;
    uint32_t block_size;
    uint32_t block_extra;
    uint64_t blocks_offset;
    uint32_t blocks_count;
    uint32_t blocks_allocated;
    uint32_t image_type;
    bool writable;
    vdisk_format_t format;
    unsigned create_flags;
};

struct vdisk_driver {
    vdisk_format_t format;
    const char *name;
    int (*probe)(FILE *fp);
    int (*open)(vdisk_t *disk);
    int (*create)(vdisk_t *disk, uint64_t size_bytes);
    int (*create_sparse)(vdisk_t *disk, uint64_t size_bytes);
    int (*read)(vdisk_t *disk, uint64_t offset, void *buf, size_t len);
    int (*write)(vdisk_t *disk, uint64_t offset, const void *buf, size_t len);
    int (*flush)(vdisk_t *disk);
    void (*close)(vdisk_t *disk);
};

extern const vdisk_driver_t vdisk_raw_driver;
extern const vdisk_driver_t vdisk_vdi_driver;
extern const vdisk_driver_t vdisk_vhd_driver;
extern const vdisk_driver_t vdisk_vmdk_driver;

int vdisk_vdi_inspect(FILE *fp, vdisk_info_t *info);
int vdisk_vhd_inspect(FILE *fp, vdisk_info_t *info);
int vdisk_vmdk_inspect(const char *path, FILE *fp, vdisk_info_t *info);

int vdisk_host_seek(FILE *fp, uint64_t off);
int vdisk_host_read(FILE *fp, uint64_t off, void *buf, size_t len);
int vdisk_host_write(FILE *fp, uint64_t off, const void *buf, size_t len);
int vdisk_host_get_size(FILE *fp, uint64_t *size_out);
