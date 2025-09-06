#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "fileutil.h"
#include "mbr.h"

#define MBR_SIZE        512
#define MBR_PART_OFFSET 446
#define MBR_SIG_OFFSET  510

typedef struct {
    uint8_t boot_flag;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t sectors;
} __attribute__((packed)) MbrPartEntry;

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------

static void set_chs(uint8_t out[3], uint32_t lba) {
    // Very crude CHS encoding (not BIOS-accurate).
    // Enough to make some partition tools happy.
    uint32_t cyl = lba / (63 * 255);
    uint32_t tmp = lba % (63 * 255);
    uint32_t head = tmp / 63;
    uint32_t sect = (tmp % 63) + 1;

    out[0] = head & 0xFF;
    out[1] = ((sect & 0x3F) | ((cyl >> 2) & 0xC0));
    out[2] = cyl & 0xFF;
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------

int mbr_init_empty(const char* img_path) {
    unsigned char buf[MBR_SIZE];
    memset(buf, 0, sizeof(buf));
    buf[MBR_SIG_OFFSET]     = 0x55;
    buf[MBR_SIG_OFFSET + 1] = 0xAA;
    return file_write_at(buf, sizeof(buf), 0, img_path);
}

int mbr_read(unsigned char out[MBR_SIZE], const char* img_path) {
    return file_read_at(out, MBR_SIZE, 0, img_path);
}

int mbr_write(const unsigned char mbr[MBR_SIZE], const char* img_path) {
    return file_write_at(mbr, MBR_SIZE, 0, img_path);
}

int mbr_add_partition(const char* img_path, int index, uint8_t type,
                      uint64_t start_bytes, uint64_t size_bytes) {
    if (index < 1 || index > 4) {
        fprintf(stderr, "error: MBR only supports partitions 1-4\n");
        return -1;
    }

    unsigned char buf[MBR_SIZE];
    if (mbr_read(buf, img_path) != 0) {
        fprintf(stderr, "error: failed to read MBR from %s\n", img_path);
        return -1;
    }

    MbrPartEntry* pe = (MbrPartEntry*)(buf + MBR_PART_OFFSET);
    pe += (index - 1);

    uint32_t lba_start = (uint32_t)(start_bytes / 512);
    uint32_t sectors   = (uint32_t)(size_bytes  / 512);

    pe->boot_flag = 0x00;
    set_chs(pe->chs_start, lba_start);
    pe->type = type;
    set_chs(pe->chs_end, lba_start + sectors - 1);
    pe->lba_start = lba_start;
    pe->sectors   = sectors;

    buf[MBR_SIG_OFFSET]     = 0x55;
    buf[MBR_SIG_OFFSET + 1] = 0xAA;

    return mbr_write(buf, img_path);
}

int mbr_print(const char* img_path) {
    unsigned char buf[MBR_SIZE];
    if (mbr_read(buf, img_path) != 0) {
        fprintf(stderr, "error: failed to read MBR from %s\n", img_path);
        return -1;
    }

    MbrPartEntry* pe = (MbrPartEntry*)(buf + MBR_PART_OFFSET);
    printf("MBR Partition Table for %s:\n", img_path);
    for (int i = 0; i < 4; i++) {
        if (pe[i].type == 0) continue;
        printf("  Part %d: type=0x%02X start=%" PRIu32 " sectors=%" PRIu32 "\n",
               i+1, pe[i].type, pe[i].lba_start, pe[i].sectors);
    }

    return 0;
}
