// src/cmd_gpt.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "cmd.h"
#include "gpt.h"
#include "use.h"
#include "parse.h"

#define DEFAULT_SECTOR 512u
#define DEFAULT_ENTRIES 128u
#define DEFAULT_ENTRY_SIZE 128u

// local adapter so we can keep using parse_size(...)
static uint64_t parse_size(const char *s, int *ok) {
    long long v = parse_size_bytes(s);
    if (v < 0) { if (ok) *ok = 0; return 0; }
    if (ok) *ok = 1;
    return (uint64_t)v;
}

// Map simple aliases -> GPT type GUIDs (on-disk byte layout)
static int guid_for_alias(const char *alias, uint8_t out[16]) {
    if (!alias) return 0;

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

    if (strcmp(alias, "linuxfs") == 0 || strcmp(alias, "linux") == 0) { memcpy(out, LINUXFS, 16); return 1; }
    if (strcmp(alias, "efi")     == 0)                                  { memcpy(out, EFI,     16); return 1; }
    if (strcmp(alias, "msbasic") == 0 || strcmp(alias, "ntfs") == 0 ||
        strcmp(alias, "fat32")   == 0)                                  { memcpy(out, MS_BASIC,16); return 1; }

    return 0;
}

static int sub_init(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "gpt init <img|/dev/X> [--entries N] [--sector 512|4096]\n"); return 2; }

    const char *img = NULL;
    if (!resolve_image_or_dev(argv[2], &img)) { fprintf(stderr, "gpt: cannot resolve %s\n", argv[2]); return 1; }

    uint32_t sector = DEFAULT_SECTOR;
    uint32_t entries = DEFAULT_ENTRIES;
    uint32_t entry_size = DEFAULT_ENTRY_SIZE;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) {
            entries = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--sector") == 0 && i + 1 < argc) {
            sector = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "gpt init: unknown arg %s\n", argv[i]);
            return 2;
        }
    }

    int rc = gpt_init_fresh(img, sector, entries, entry_size);
    if (rc != 0) { fprintf(stderr, "gpt init: failed (rc=%d)\n", rc); return 1; }

    printf("GPT initialized: sector=%u entries=%u entry-size=%u\n", sector, entries, entry_size);
    return 0;
}

static int sub_add(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "gpt add <img|/dev/X> [--type linuxfs] [--name NAME] [--start 1MiB|lba:N] [--size <N>|100%%]\n"); return 2; }

    const char *img = NULL;
    if (!resolve_image_or_dev(argv[2], &img)) { fprintf(stderr, "gpt: cannot resolve %s\n", argv[2]); return 1; }

    char type_alias[32] = "linuxfs";
    const char *name = NULL;

	int have_start = 0, have_size = 0;
	int size_is_pct = 0;
	uint64_t start_lba = 0, size_lba = 0;

    const uint32_t sector = DEFAULT_SECTOR;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            strncpy(type_alias, argv[++i], sizeof type_alias - 1);
            type_alias[sizeof type_alias - 1] = 0;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            const char *s = argv[++i];
            if (strncmp(s, "lba:", 4) == 0) {
                start_lba = strtoull(s + 4, NULL, 10);
            } else {
                int ok = 0; uint64_t bytes = parse_size(s, &ok);
                if (!ok) { fprintf(stderr, "gpt add: invalid --start\n"); return 2; }
                start_lba = bytes / sector;
                uint64_t align = (1024ull * 1024ull) / sector; // 1 MiB boundary
                if (start_lba % align) start_lba = ((start_lba / align) + 1) * align;
            }
            have_start = 1;
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            const char *s = argv[++i];
            if (strcmp(s, "100%") == 0) { size_is_pct = 1; have_size = 1; }
            else {
                int ok = 0; uint64_t bytes = parse_size(s, &ok);
                if (!ok || bytes == 0) { fprintf(stderr, "gpt add: invalid --size\n"); return 2; }
                size_lba = bytes / sector;
                if (size_lba == 0) size_lba = 1;
                have_size = 1;
            }
        } else {
            fprintf(stderr, "gpt add: unknown arg %s\n", argv[i]);
            return 2;
        }
    }

    GptHeader h;
    if (!gpt_read_header(img, &h, 1)) { fprintf(stderr, "gpt add: not a GPT disk\n"); return 1; }

    // Find next-free LBA from current entries
    GptEntry *ents = NULL;
    if (!gpt_read_entries(img, &h, &ents)) { fprintf(stderr, "gpt add: failed reading entries\n"); return 1; }

    uint64_t next_free = h.first_usable_lba;
    for (unsigned i = 0; i < h.num_part_entries; ++i) {
        const GptEntry *e = (const GptEntry *)((const uint8_t*)ents + (size_t)i * h.part_entry_size);
        int empty = 1;
        for (int k = 0; k < 16; ++k) if (e->type_guid[k]) { empty = 0; break; }
        if (empty) continue;
        if (e->last_lba + 1 > next_free) next_free = e->last_lba + 1;
    }
    free(ents);

    if (!have_start) {
        uint64_t align = (1024ull * 1024ull) / sector;
        if (next_free < h.first_usable_lba) next_free = h.first_usable_lba;
        if (next_free % align) next_free = ((next_free / align) + 1) * align;
        start_lba = next_free;
    }

    uint64_t last_lba = 0;
    if (!have_size || size_is_pct) {
        last_lba = h.last_usable_lba;
    } else {
        last_lba = start_lba + size_lba - 1;
        if (last_lba > h.last_usable_lba) last_lba = h.last_usable_lba;
    }

    if (start_lba < h.first_usable_lba || start_lba > h.last_usable_lba || start_lba > last_lba) {
        fprintf(stderr, "gpt add: start/size outside usable range\n"); return 2;
    }

    uint8_t type_guid[16];
    if (!guid_for_alias(type_alias, type_guid)) {
        fprintf(stderr, "gpt add: unknown type '%s'\n", type_alias);
        return 2;
    }

    int rc = gpt_add_partition_lba(img, type_guid, name, start_lba, last_lba);
    if (rc != 0) { fprintf(stderr, "gpt add: failed (rc=%d)\n", rc); return 1; }

    double mb = (double)(last_lba - start_lba + 1) * 512.0 / (1024.0 * 1024.0);
    printf("Added %s  start=%llu  end=%llu  size=%.1fMB  name=%s\n",
           type_alias,
           (unsigned long long)start_lba,
           (unsigned long long)last_lba,
           mb,
           name ? name : "");
    return 0;
}

static int sub_print(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "gpt print <img|/dev/X>\n"); return 2; }

    const char *img = NULL;
    if (!resolve_image_or_dev(argv[2], &img)) { fprintf(stderr, "gpt: cannot resolve %s\n", argv[2]); return 1; }

    GptHeader h;
    if (!gpt_read_header(img, &h, 1)) { fprintf(stderr, "gpt: not a GPT disk or read error\n"); return 1; }

    char disk_guid[37]; gpt_guid_to_str(h.disk_guid, disk_guid);
    printf("Disk: %s  Sector: 512\n", img);
    printf("Disk GUID: %s\n", disk_guid);
    printf("Primary GPT: LBA %llu | Array: LBA %llu  (entries=%u, size=%u)\n",
           (unsigned long long)h.current_lba,
           (unsigned long long)h.part_entry_lba,
           h.num_part_entries, h.part_entry_size);
    printf("Backup  GPT: LBA %llu\n\n", (unsigned long long)h.backup_lba);

    GptEntry *ents = NULL;
    if (!gpt_read_entries(img, &h, &ents)) { fprintf(stderr, "gpt: failed reading entries\n"); return 1; }

    printf("Idx  Start LBA     End LBA       Size        Type       Name\n");
    printf("---  ------------  ------------  ----------  ---------- ----------------\n");

    for (unsigned i = 0; i < h.num_part_entries; ++i) {
        const GptEntry *e = (const GptEntry *)((const uint8_t*)ents + (size_t)i * h.part_entry_size);
        int empty = 1; for (int k = 0; k < 16; ++k) if (e->type_guid[k]) { empty = 0; break; }
        if (empty) continue;

        const char *alias = gpt_alias_for_type(e->type_guid);
        char type_guid[37]; gpt_guid_to_str(e->type_guid, type_guid);

        uint16_t u16name[36]; memcpy(u16name, e->name_utf16, sizeof u16name);
        char name[128]; gpt_utf16le_to_utf8(u16name, 36, name, sizeof name);

        unsigned long long size_lba = (unsigned long long)(e->last_lba - e->first_lba + 1);
        double size_mb = (double)size_lba * 512.0 / (1024.0 * 1024.0);

        printf("%3u  %12llu  %12llu  %8.1fMB  %-10s %s\n",
               i + 1,
               (unsigned long long)e->first_lba,
               (unsigned long long)e->last_lba,
               size_mb,
               alias ? alias : "(guid)",
               name[0] ? name : "");
    }
    free(ents);
    return 0;
}

int cmd_gpt(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "init")  == 0) return sub_init(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "add")   == 0) return sub_add(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "print") == 0) return sub_print(argc, argv);

    fprintf(stderr, "gpt <init|add|print> ...\n");
    return 2;
}
