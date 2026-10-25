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

typedef struct {
    vdisk_format_t format;
    uint64_t host_size_bytes;
    uint64_t logical_size_bytes;
    uint64_t data_offset;
    uint64_t blocks_offset;
    uint32_t image_type;
    uint32_t sector_size;
    uint32_t block_size;
    uint32_t blocks_count;
    uint32_t blocks_allocated;
    uint32_t map_allocated_entries;
    uint32_t map_unallocated_entries;
    uint32_t map_invalid_entries;
    uint32_t map_duplicate_entries;
    unsigned checks_passed;
    unsigned checks_failed;
    bool supported;
    bool structurally_valid;
    char variant[32];
} vdisk_info_t;

/* Inspect container bytes without trusting the filename extension.
   This operation is read-only. Recognized-but-unsupported formats are
   reported with supported=false. */
int vdisk_inspect(const char *path, vdisk_info_t *info);


#define VDISK_CREATE_SPARSE                0x00000001u
#define VDISK_CREATE_VMDK_LAYOUT_MASK      0x00000f00u
#define VDISK_CREATE_VMDK_FLAT             0x00000100u
#define VDISK_CREATE_VMDK_MONOLITHIC_FLAT  0x00000200u
#define VDISK_CREATE_VMDK_MONOLITHIC_SPARSE 0x00000300u
#define VDISK_CREATE_VMDK_SPLIT_FLAT       0x00000400u
#define VDISK_CREATE_VMDK_SPLIT_SPARSE     0x00000500u

int vdisk_open(const char *path, vdisk_format_t format, vdisk_t **out);
int vdisk_create(const char *path, vdisk_format_t format, uint64_t size_bytes, vdisk_t **out);
int vdisk_create_ex(const char *path, vdisk_format_t format, uint64_t size_bytes,
                    unsigned flags, vdisk_t **out);
int vdisk_create_ex_uuid(const char *path, vdisk_format_t format, uint64_t size_bytes,
                         unsigned flags, const uint8_t uuid[16], vdisk_t **out);
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
