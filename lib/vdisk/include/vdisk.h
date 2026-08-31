#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VDISK_FMT_AUTO = 0,
    VDISK_FMT_RAW,
    VDISK_FMT_VDI,
    VDISK_FMT_VMDK,
    VDISK_FMT_VHD,
    VDISK_FMT_QCOW2
} vdisk_format_t;

typedef struct vdisk vdisk_t;

int vdisk_open(const char *path, vdisk_format_t format, vdisk_t **out);
int vdisk_create(const char *path, vdisk_format_t format, uint64_t size_bytes, vdisk_t **out);
void vdisk_close(vdisk_t *disk);

int vdisk_read(vdisk_t *disk, uint64_t offset, void *buf, size_t len);
int vdisk_write(vdisk_t *disk, uint64_t offset, const void *buf, size_t len);
int vdisk_flush(vdisk_t *disk);

uint64_t vdisk_get_size(const vdisk_t *disk);
bool vdisk_is_writable(const vdisk_t *disk);
vdisk_format_t vdisk_get_format(const vdisk_t *disk);
const char *vdisk_get_path(const vdisk_t *disk);

vdisk_format_t vdisk_format_from_extension(const char *path);
const char *vdisk_format_name(vdisk_format_t format);

#ifdef __cplusplus
}
#endif
