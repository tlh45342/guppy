// src/vfs_fat.c

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "vblk.h"
#include "vfs.h"
#include "vfs_core.h"

#pragma pack(push,1)
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bps;
    uint8_t  spc;
    uint16_t rsvd;
    uint8_t  fats;
    uint16_t root_entries;
    uint16_t total16;
    uint8_t  media;
    uint16_t fatsz16;
    uint16_t spt, heads;
    uint32_t hidden, total32;
    union {
        struct {
            uint8_t  drv, nt_flags, sig;
            uint32_t vol_id;
            char     vol_label[11];
            char     fs_type[8];   // "FAT12   " or "FAT16   "
        } f16;
        struct {
            uint32_t fatsz32;
            uint16_t ext_flags, fsver;
            uint32_t rootclus;
            uint16_t fsinfo, bkboot;
            uint8_t  rsvd2[12];
            uint8_t  drv, nt_flags, sig;
            uint32_t vol_id;
            char     vol_label[11];
            char     fs_type[8];   // "FAT32   "
        } f32;
    } u;
    uint8_t  bootcode[420];
    uint16_t sig55AA;          // 0xAA55
} bpb_t;
#pragma pack(pop)

static bool read_blocks(vblk_t *dev, uint32_t lba, uint32_t count, void *dst){
    return vblk_read_blocks(dev, lba, count, dst);
}

static bool is_fat_signature(const bpb_t *bpb){
    if (bpb->sig55AA != 0xAA55) return false;
    if (!memcmp(bpb->u.f16.fs_type, "FAT12   ", 8)) return true;
    if (!memcmp(bpb->u.f16.fs_type, "FAT16   ", 8)) return true;
    if (!memcmp(bpb->u.f32.fs_type, "FAT32   ", 8)) return true;
    if (!memcmp(bpb->u.f16.fs_type, "FAT     ", 8)) return true; // some tools write "FAT"
    return false;
}

// --- replace your existing definition of fat_probe(...) with this ---
static bool fat_probe(vblk_t *dev, char *label_out, size_t label_cap)
{
    (void)label_out;
    (void)label_cap;
    bpb_t b;
    if (!read_blocks(dev, 0, 1, &b)) return false;
    return is_fat_signature(&b);
}

const vfs_driver_t VFS_FAT = {
    .name  = "fat",
    .probe = fat_probe,
};
