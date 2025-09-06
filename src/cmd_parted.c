// src/cmd_parted.c
// Guppy: "parted -l" style listing of partition tables (MBR + GPT)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>

#include "cmd.h"  // declare: int cmd_parted(int argc, char** argv);

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

#define MBR_SIG_OFFSET 510
#define MBR_PART_OFFSET 446

static bool read_lba(FILE *f, uint64_t lba, void *buf, size_t sz) {
    if (sz % SECTOR_SIZE != 0) return false;
    if (fseeko(f, (off_t)(lba * SECTOR_SIZE), SEEK_SET) != 0) return false;
    return fread(buf, 1, sz, f) == sz;
}

static uint16_t le16(const void *p){ const uint8_t *b=p; return (uint16_t)(b[0] | (b[1]<<8)); }
static uint32_t le32(const void *p){ const uint8_t *b=p; return (uint32_t)(b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24)); }
static uint64_t le64(const void *p){ const uint8_t *b=p; return (uint64_t)le32(b) | ((uint64_t)le32(b+4)<<32); }

#pragma pack(push,1)
typedef struct {
    uint8_t  boot_indicator;     // 0x80 bootable, 0x00 otherwise
    uint8_t  start_chs[3];
    uint8_t  type;               // system ID
    uint8_t  end_chs[3];
    uint32_t start_lba;          // little-endian
    uint32_t sectors;            // little-endian
} mbr_part_t;

typedef struct {
    uint8_t  jmp[440];
    uint32_t disk_sig;
    uint16_t reserved;
    mbr_part_t part[4];
    uint16_t sig; // 0xAA55
} mbr_t;

typedef struct {
    uint8_t  sig[8];            // "EFI PART"
    uint32_t rev;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t part_array_lba;
    uint32_t part_count;
    uint32_t part_entry_size;   // typically 128
    uint32_t part_array_crc32;
    uint8_t  rest[SECTOR_SIZE - 92]; // pad to sector
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name_utf16[72];    // UTF-16LE, not necessarily NUL-terminated
} gpt_entry_t;
#pragma pack(pop)

// --- helpers ---
static void print_guid_le(const uint8_t g[16]) {
    // GUID on disk is little-endian in first 3 fields.
    uint32_t d1 = (uint32_t)(g[3]<<24 | g[2]<<16 | g[1]<<8 | g[0]);
    uint16_t d2 = (uint16_t)(g[5]<<8 | g[4]);
    uint16_t d3 = (uint16_t)(g[7]<<8 | g[6]);
    printf("%08x-%04x-%04x-", d1, d2, d3);
    for (int i=8;i<10;i++) printf("%02x", g[i]);
    printf("-");
    for (int i=10;i<16;i++) printf("%02x", g[i]);
}

static void utf16le_to_utf8(const uint16_t *in, size_t in_len, char *out, size_t out_sz) {
    // Simple BMP-only; stop on NUL or when out is full.
    size_t oi=0;
    for (size_t i=0; i<in_len; i++) {
        uint16_t ch = in[i];
        if (ch == 0x0000) break;
        if (ch < 0x80) {
            if (oi+1 >= out_sz) break;
            out[oi++] = (char)ch;
        } else if (ch < 0x800) {
            if (oi+2 >= out_sz) break;
            out[oi++] = (char)(0xC0 | (ch>>6));
            out[oi++] = (char)(0x80 | (ch & 0x3F));
        } else {
            if (oi+3 >= out_sz) break;
            out[oi++] = (char)(0xE0 | (ch>>12));
            out[oi++] = (char)(0x80 | ((ch>>6) & 0x3F));
            out[oi++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    if (oi < out_sz) out[oi] = '\0';
    else out[out_sz-1] = '\0';
}

static const char* mbr_type_desc(uint8_t t) {
    switch (t) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32M";
        case 0x05: return "Extended";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT/HPFS";
        case 0x0b: return "FAT32 (CHS)";
        case 0x0c: return "FAT32 (LBA)";
        case 0x0e: return "FAT16 (LBA)";
        case 0x0f: return "Extended (LBA)";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux filesystem";
        case 0xee: return "GPT Protective";
        default:   return "Unknown";
    }
}

static bool is_all_zero(const uint8_t *p, size_t n) {
    for (size_t i=0;i<n;i++) if (p[i]!=0) return false;
    return true;
}

// --- printing ---

static void print_mbr(const mbr_t *m) {
    printf("Partition Table: MBR\n");
    for (int i=0;i<4;i++) {
        const mbr_part_t *p = &m->part[i];
        if (p->type == 0x00 || (p->start_lba==0 && p->sectors==0))
            continue;
        uint32_t start = le32(&p->start_lba);
        uint32_t count = le32(&p->sectors);
        printf("  %d: %s  Boot:%s  Type:0x%02x (%s)  Start LBA:%" PRIu32 "  Sectors:%" PRIu32 "  Size:%.2f MiB\n",
               i+1,
               (p->type==0x05 || p->type==0x0f) ? "Extended" : "Primary",
               (p->boot_indicator==0x80) ? "Yes":"No",
               p->type, mbr_type_desc(p->type),
               start, count, (double)count * SECTOR_SIZE / (1024.0*1024.0));
    }
}

static bool looks_like_gpt_header(const gpt_header_t *h) {
    static const uint8_t sig[8] = {'E','F','I',' ','P','A','R','T'};
    return memcmp(h->sig, sig, 8) == 0 &&
           h->part_entry_size >= sizeof(gpt_entry_t) &&
           h->header_size >= 92 && h->header_size <= SECTOR_SIZE;
}

static void print_gpt(FILE *f, const gpt_header_t *gh) {
    printf("Partition Table: GPT\n");
    printf("  Disk GUID: "); print_guid_le(gh->disk_guid); printf("\n");
    printf("  Usable LBAs: %" PRIu64 " .. %" PRIu64 "\n",
           le64(&gh->first_usable_lba), le64(&gh->last_usable_lba));
    printf("  Entries @ LBA: %" PRIu64 "  Count: %" PRIu32 "  Size: %" PRIu32 "\n",
           le64(&gh->part_array_lba), le32(&gh->part_count), le32(&gh->part_entry_size));

    uint64_t tbl_lba = le64(&gh->part_array_lba);
    uint32_t count   = le32(&gh->part_count);
    uint32_t entsz   = le32(&gh->part_entry_size);
    uint64_t bytes   = (uint64_t)count * entsz;
    uint32_t sectors = (uint32_t)((bytes + SECTOR_SIZE - 1)/SECTOR_SIZE);

    // read entries in chunk(s)
    uint8_t sector[SECTOR_SIZE];
    for (uint32_t s=0; s<sectors; s++) {
        if (!read_lba(f, tbl_lba + s, sector, SECTOR_SIZE)) {
            fprintf(stderr, "error: failed reading GPT entries\n");
            return;
        }
        uint32_t entries_in_this_sector = SECTOR_SIZE / entsz;
        for (uint32_t i=0; i<entries_in_this_sector; i++) {
            uint64_t idx = (uint64_t)s * entries_in_this_sector + i;
            if (idx >= count) break;
            const gpt_entry_t *e = (const gpt_entry_t*)(sector + i*entsz);
            if (is_all_zero((const uint8_t*)e, sizeof(gpt_entry_t))) continue;

            char name[128];
            utf16le_to_utf8(e->name_utf16, 72, name, sizeof(name));

            printf("  %2" PRIu64 ": ", idx+1);
            if (name[0]) printf("Name=\"%s\"  ", name);
            printf("Type="); print_guid_le(e->type_guid);
            printf("  UUID="); print_guid_le(e->unique_guid);
            printf("\n      First LBA:%" PRIu64 "  Last LBA:%" PRIu64 "  Attr:0x%016" PRIx64 "  Size:%.2f MiB\n",
                   le64(&e->first_lba), le64(&e->last_lba), le64(&e->attrs),
                   (double)((le64(&e->last_lba) - le64(&e->first_lba) + 1) * SECTOR_SIZE) / (1024.0*1024.0));
        }
    }
}

static int do_parted_list(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
        return 2;
    }

    // Get image size
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); return 2; }
    off_t fsz = ftello(f);
    if (fsz < (off_t)SECTOR_SIZE) {
        fprintf(stderr, "error: file too small to be a disk image\n");
        fclose(f);
        return 2;
    }
    uint64_t total_sectors = (uint64_t)fsz / SECTOR_SIZE;
    fseeko(f, 0, SEEK_SET);

    uint8_t s0[SECTOR_SIZE], s1[SECTOR_SIZE];
    if (!read_lba(f, 0, s0, SECTOR_SIZE)) { fclose(f); return 2; }

    printf("%s:\n", path);
    printf("  Size: %" PRIu64 " bytes (%.2f MiB), Sectors: %" PRIu64 ", Sector size: %d\n",
           (uint64_t)fsz, (double)fsz/(1024.0*1024.0), total_sectors, SECTOR_SIZE);

    const mbr_t *m = (const mbr_t*)s0;
    bool mbr_valid = (le16(&s0[MBR_SIG_OFFSET]) == 0xAA55);

    bool printed_table = false;

    if (mbr_valid) {
        // Check for Protective MBR (0xEE)
        bool has_ee = false;
        for (int i=0;i<4;i++) if (m->part[i].type == 0xEE) { has_ee = true; break; }

        if (has_ee) {
            // Probe GPT header at LBA 1
            if (!read_lba(f, 1, s1, SECTOR_SIZE)) { fclose(f); return 2; }
            const gpt_header_t *gh = (const gpt_header_t*)s1;
            if (looks_like_gpt_header(gh)) {
                print_gpt(f, gh);
                printed_table = true;
            } else {
                // Protective MBR but no valid GPT header? Still print MBR.
                print_mbr(m);
                printed_table = true;
            }
        } else {
            print_mbr(m);
            printed_table = true;
        }
    } else {
        // No valid MBR sig; still try GPT header at LBA 1
        if (!read_lba(f, 1, s1, SECTOR_SIZE)) { fclose(f); return 2; }
        const gpt_header_t *gh = (const gpt_header_t*)s1;
        if (looks_like_gpt_header(gh)) {
            print_gpt(f, gh);
            printed_table = true;
        }
    }

    if (!printed_table) {
        printf("Partition Table: (none detected)\n");
    }

    fclose(f);
    return 0;
}

// --- public command entry ---
// Usage: parted -l <image>
int cmd_parted(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("Usage: parted -l <disk.img>\n");
        return 0;
    }
    if (strcmp(argv[1], "-l") != 0) {
        fprintf(stderr, "parted: unknown option '%s'\n", argv[1]);
        return 2;
    }
    if (argc < 3) {
        fprintf(stderr, "parted -l: missing disk image path\n");
        return 2;
    }
    return do_parted_list(argv[2]);
}