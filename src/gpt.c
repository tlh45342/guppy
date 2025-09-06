// src/gpt.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "gpt.h"

#ifndef SECTOR_BYTES_DEFAULT
#define SECTOR_BYTES_DEFAULT 512u
#endif

// ---------------------------- low-level file I/O ----------------------------

static int read_at_path(const char *path, uint64_t off, void *buf, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t r = fread(buf, 1, n, f);
    fclose(f);
    return (r == n) ? 0 : -1;
}

static int write_at_path(const char *path, uint64_t off, const void *buf, size_t n) {
    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f) return -1;
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t w = fwrite(buf, 1, n, f);
    fflush(f);
    fclose(f);
    return (w == n) ? 0 : -1;
}

static int file_size_bytes(const char *path, uint64_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseeko(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    off_t sz = ftello(f);
    fclose(f);
    if (sz < 0) return -1;
    *out = (uint64_t)sz;
    return 0;
}

// -------------------------------- GUID utils --------------------------------

static uint32_t be32_from_le_bytes(const uint8_t b[4]) {
    return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[0];
}
static uint16_t be16_from_le_bytes(const uint8_t b[2]) {
    return ((uint16_t)b[1] << 8) | (uint16_t)b[0];
}

void gpt_guid_to_str(const uint8_t g[16], char out[37]) {
    // GPT stores first 3 fields little-endian on disk.
    uint32_t d1 = be32_from_le_bytes(&g[0]);
    uint16_t d2 = be16_from_le_bytes(&g[4]);
    uint16_t d3 = be16_from_le_bytes(&g[6]);
    const uint8_t *d4 = &g[8];   // 2 bytes (big-endian byte order when printed)
    const uint8_t *d5 = &g[10];  // 6 bytes
    // XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
    snprintf(out, 37,
             "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             d1, d2, d3, d4[0], d4[1], d5[0], d5[1], d5[2], d5[3], d5[4], d5[5]);
}

const char *gpt_alias_for_type(const uint8_t type_guid[16]) {
    // On-disk byte layout (little-endian for first 3 fields).
    // Linux filesystem: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
    static const uint8_t LINUXFS[16] = {
        0xAF,0x3D,0xC6,0x0F, 0x83,0x84, 0x72,0x47, 0x8E,0x79, 0x3D,0x69,0xD8,0x47,0x7D,0xE4
    };
    // EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    static const uint8_t EFI[16] = {
        0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B
    };
    // Microsoft Basic Data: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
    static const uint8_t MS_BASIC[16] = {
        0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7
    };
    // Linux swap: 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
    static const uint8_t LINUXSWAP[16] = {
        0x6D,0xFD,0x57,0x06, 0xAB,0xA4, 0xC4,0x43, 0x84,0xE5, 0x09,0x33,0xC8,0x4B,0x4F,0x4F
    };

    if (memcmp(type_guid, LINUXFS,   16) == 0) return "linuxfs";
    if (memcmp(type_guid, EFI,       16) == 0) return "efi";
    if (memcmp(type_guid, MS_BASIC,  16) == 0) return "msbasic";
    if (memcmp(type_guid, LINUXSWAP, 16) == 0) return "linuxswap";
    return NULL;
}

// ---------------------------- UTF-16LE → UTF-8 ------------------------------

void gpt_utf16le_to_utf8(const uint16_t *u16, int max_units, char *out, int outsz) {
    int oi = 0;
    for (int i = 0; i < max_units && u16[i] != 0; ++i) {
        unsigned c = u16[i];
        if (c < 0x80) {
            if (oi + 1 >= outsz) break;
            out[oi++] = (char)c;
        } else if (c < 0x800) {
            if (oi + 2 >= outsz) break;
            out[oi++] = (char)(0xC0 | (c >> 6));
            out[oi++] = (char)(0x80 | (c & 0x3F));
        } else {
            if (oi + 3 >= outsz) break;
            out[oi++] = (char)(0xE0 | (c >> 12));
            out[oi++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[oi++] = (char)(0x80 | (c & 0x3F));
        }
    }
    if (outsz > 0) out[oi < outsz ? oi : outsz - 1] = '\0';
}

// ------------------------------ GPT readers ---------------------------------

// NOTE: third parameter is treated as "use_primary": nonzero -> primary, 0 -> backup.
bool gpt_read_header(const char *img, GptHeader *out, uint32_t use_primary) {
    if (!img || !out) return false;

    uint64_t bytes = 0;
    if (file_size_bytes(img, &bytes) != 0) return false;
    if (bytes < 2 * SECTOR_BYTES_DEFAULT) return false;

    uint64_t lba = use_primary ? 1 : (bytes / SECTOR_BYTES_DEFAULT) - 1;

    GptHeader h;
    if (read_at_path(img, lba * SECTOR_BYTES_DEFAULT, &h, sizeof h) != 0) return false;

    if (memcmp(h.signature, "EFI PART", 8) != 0) return false;
    if (h.header_size < 92) return false;

    *out = h;
    return true;
}

bool gpt_read_entries(const char *img, const GptHeader *h, GptEntry **out_entries) {
    if (!img || !h || !out_entries) return false;

    uint64_t total_bytes = (uint64_t)h->num_part_entries * (uint64_t)h->part_entry_size;
    if (total_bytes == 0) return false;

    uint8_t *buf = (uint8_t*)calloc(1, (size_t)total_bytes);
    if (!buf) return false;

    if (read_at_path(img, h->part_entry_lba * SECTOR_BYTES_DEFAULT, buf, (size_t)total_bytes) != 0) {
        free(buf);
        return false;
    }

    *out_entries = (GptEntry*)buf; // caller frees
    return true;
}

// ------------------------------ GPT writers ---------------------------------

static void rand_guid(uint8_t g[16]) {
    // Non-crypto random GUID v4
    for (int i = 0; i < 16; i++) g[i] = (uint8_t)(rand() & 0xFF);
    g[6] = (uint8_t)((g[6] & 0x0F) | 0x40); // version 4 (0100)
    g[8] = (uint8_t)((g[8] & 0x3F) | 0x80); // variant (10xx)
}

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

int gpt_init_fresh(const char *path,
                   uint32_t sector,
                   uint32_t entries,
                   uint32_t entry_size)
{
    if (sector != 512 && sector != 4096) return -2;
    if (entry_size != 128) return -3;
    if (entries == 0) entries = 128;

    uint64_t bytes = 0;
    if (file_size_bytes(path, &bytes) != 0) return -4;
    if (bytes < (uint64_t)sector * 128ull) return -5;
    if ((bytes % sector) != 0) return -6;

    uint64_t total_lba = bytes / sector;
    uint64_t last_lba  = total_lba - 1;

    uint64_t ents_bytes = (uint64_t)entries * entry_size;
    uint64_t ents_lba   = (ents_bytes + sector - 1) / sector;

    // 1 MiB alignment for first usable LBA
    uint64_t align_lba = (1024ull * 1024ull) / sector;

    uint64_t primary_hdr_lba  = 1;
    uint64_t primary_ents_lba = 2;
    uint64_t first_usable     = align_up(2 + ents_lba, align_lba);

    uint64_t backup_hdr_lba   = last_lba;
    uint64_t backup_ents_lba  = last_lba - ents_lba; // [backup_ents_lba .. backup_hdr_lba-1]
    if (backup_ents_lba <= first_usable) return -7;
    uint64_t last_usable = backup_ents_lba - 1;

    // Clear primary+backup entry arrays
    {
        size_t one = (size_t)sector;
        void *zer = calloc(1, one);
        if (!zer) return -8;
        for (uint64_t i = 0; i < ents_lba; i++) {
            if (write_at_path(path, (primary_ents_lba + i) * (uint64_t)sector, zer, one) != 0) { free(zer); return -9; }
            if (write_at_path(path, (backup_ents_lba  + i) * (uint64_t)sector, zer, one) != 0) { free(zer); return -10; }
        }
        free(zer);
    }

    // Primary header (string signature)
    GptHeader ph; memset(&ph, 0, sizeof ph);
    memcpy(ph.signature, "EFI PART", 8);
    ph.revision          = 0x00010000u;
    ph.header_size       = 92;
    ph.header_crc32      = 0; // omitted
    ph.current_lba       = primary_hdr_lba;
    ph.backup_lba        = backup_hdr_lba;
    ph.first_usable_lba  = first_usable;
    ph.last_usable_lba   = last_usable;
    rand_guid(ph.disk_guid);
    ph.part_entry_lba    = primary_ents_lba;
    ph.num_part_entries  = entries;
    ph.part_entry_size   = entry_size;
    ph.part_array_crc32  = 0; // omitted

    // Backup header (mirror)
    GptHeader bh = ph;
    bh.current_lba    = backup_hdr_lba;
    bh.backup_lba     = primary_hdr_lba;
    bh.part_entry_lba = backup_ents_lba;

    // Write headers
    if (write_at_path(path, primary_hdr_lba * (uint64_t)sector, &ph, sizeof ph) != 0) return -11;
    if (write_at_path(path, backup_hdr_lba  * (uint64_t)sector, &bh, sizeof bh) != 0)  return -12;

    // Protective MBR at LBA0 (type 0xEE)
    if (sector == 512) {
        unsigned char mbr[512]; memset(mbr, 0, sizeof mbr);
        mbr[510] = 0x55; mbr[511] = 0xAA;
        uint32_t lba_start = 1;
        uint32_t lba_count = (total_lba > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)(total_lba - 1);
        mbr[446 + 0x00] = 0x00; // boot flag
        mbr[446 + 0x04] = 0xEE; // type = GPT protective
        // LBA start (little-endian)
        mbr[446 + 0x08] = (uint8_t)(lba_start & 0xFF);
        mbr[446 + 0x09] = (uint8_t)((lba_start >> 8) & 0xFF);
        mbr[446 + 0x0A] = (uint8_t)((lba_start >> 16) & 0xFF);
        mbr[446 + 0x0B] = (uint8_t)((lba_start >> 24) & 0xFF);
        // sector count (little-endian)
        mbr[446 + 0x0C] = (uint8_t)(lba_count & 0xFF);
        mbr[446 + 0x0D] = (uint8_t)((lba_count >> 8) & 0xFF);
        mbr[446 + 0x0E] = (uint8_t)((lba_count >> 16) & 0xFF);
        mbr[446 + 0x0F] = (uint8_t)((lba_count >> 24) & 0xFF);
        if (write_at_path(path, 0, mbr, sizeof mbr) != 0) return -13;
    } else {
        // For 4K sector images, you can add a 4K protective MBR if desired.
        // Many tools don't require it on pure image workflows; skipping for now.
    }

    return 0;
}

static int utf8_to_utf16le(const char *in, uint16_t *out, size_t max_chars) {
    size_t i = 0;
    while (*in && i < max_chars) {
        unsigned char c = (unsigned char)*in++;
        if (c < 0x80) { out[i++] = (uint16_t)c; }
        else {
            // Minimal BMP-only: skip non-ASCII and use '?'
            while ((*in & 0xC0) == 0x80) ++in; // skip continuation bytes
            out[i++] = (uint16_t)'?';
        }
    }
    return (int)i;
}

int gpt_add_partition_lba(const char *path,
                          const uint8_t type_guid[16],
                          const char *name_utf8,
                          uint64_t first_lba,
                          uint64_t last_lba)
{
    // Read primary header (callers pass 1 for "primary")
    GptHeader h;
    if (!gpt_read_header(path, &h, 1)) return -1;

    // Load entry array (primary)
    size_t entry_bytes = (size_t)h.num_part_entries * (size_t)h.part_entry_size;
    uint8_t *ents = (uint8_t*)calloc(1, entry_bytes);
    if (!ents) return -2;

    if (read_at_path(path, h.part_entry_lba * SECTOR_BYTES_DEFAULT, ents, entry_bytes) != 0) {
        free(ents); return -3;
    }

    // Find free slot
    unsigned slot = h.num_part_entries;
    for (unsigned i = 0; i < h.num_part_entries; ++i) {
        const GptEntry *e = (const GptEntry*)(ents + (size_t)i * h.part_entry_size);
        int empty = 1;
        for (int k = 0; k < 16; ++k) if (e->type_guid[k]) { empty = 0; break; }
        if (empty) { slot = i; break; }
    }
    if (slot == h.num_part_entries) { free(ents); return -4; }

    // Fill new entry
    GptEntry *ne = (GptEntry*)(ents + (size_t)slot * h.part_entry_size);
    memset(ne, 0, sizeof(GptEntry));
    memcpy(ne->type_guid, type_guid, 16);
    rand_guid(ne->uniq_guid);
    ne->first_lba = first_lba;
    ne->last_lba  = last_lba;
    ne->attrs     = 0;

    if (name_utf8 && *name_utf8) {
        uint16_t u16[36]; memset(u16, 0, sizeof u16);
        utf8_to_utf16le(name_utf8, u16, 36);
        memcpy(ne->name_utf16, u16, sizeof u16);
    }

    // Write primary entries back
    if (write_at_path(path, h.part_entry_lba * SECTOR_BYTES_DEFAULT, ents, entry_bytes) != 0) {
        free(ents); return -5;
    }

    // Update backup entries via backup header
    GptHeader bh;
    if (read_at_path(path, h.backup_lba * SECTOR_BYTES_DEFAULT, &bh, sizeof bh) != 0) {
        free(ents); return -6;
    }
    if (write_at_path(path, bh.part_entry_lba * SECTOR_BYTES_DEFAULT, ents, entry_bytes) != 0) {
        free(ents); return -7;
    }

    // Re-write headers (CRCs left as zero in this minimal build)
    if (write_at_path(path, h.current_lba * SECTOR_BYTES_DEFAULT, &h, sizeof h) != 0)  { free(ents); return -8; }
    if (write_at_path(path, bh.current_lba * SECTOR_BYTES_DEFAULT, &bh, sizeof bh) != 0){ free(ents); return -9; }

    free(ents);
    return 0;
}
