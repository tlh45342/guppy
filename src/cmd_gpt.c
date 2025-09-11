// src/cmd_gpt.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#ifdef _WIN32
  #ifndef __CYGWIN__
    // portable case-insensitive compares if you're ever on MSVC
    #define strncasecmp _strnicmp
    #define strcasecmp  _stricmp
  #endif
#endif

#include "helper.h"   // resolve_image_or_dev()
#include "gpt.h"
#include "diskio.h"   // filesize_bytes()

// ---------- tiny helpers ----------
static uint64_t parse_size_bytes(const char *s) {
    // supports: 123, 1KiB, 1MiB, 1GiB, 1KB/MB/GB (base-2)
    if (!s || !*s) return 0;
    char *end = NULL;
    double val = strtod(s, &end);
    while (*end == ' ') end++;
    uint64_t mult = 1;
    if      (strncasecmp(end, "KIB", 3) == 0 || strncasecmp(end, "KB", 2) == 0) mult = 1024ull;
    else if (strncasecmp(end, "MIB", 3) == 0 || strncasecmp(end, "MB", 2) == 0) mult = 1024ull*1024ull;
    else if (strncasecmp(end, "GIB", 3) == 0 || strncasecmp(end, "GB", 2) == 0) mult = 1024ull*1024ull*1024ull;
    return (uint64_t)(val * (double)mult + 0.5);
}
static int parse_percent(const char *s) {
    if (!s) return -1;
    size_t n = strlen(s);
    if (n && s[n-1] == '%') {
        char buf[32]; if (n >= sizeof(buf)) return -1;
        memcpy(buf, s, n-1); buf[n-1] = 0;
        int p = (int)strtol(buf, NULL, 10);
        if (p >= 0 && p <= 100) return p;
    }
    return -1;
}
static void guid_linuxfs(uint8_t out[16]) {
    // Linux filesystem GUID: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
    // GPT stores first 3 fields little-endian on disk:
    const uint8_t g[16] = {
        0xAF,0x3D,0xC6,0x0F, 0x83,0x84, 0x72,0x47, 0x8E,0x79, 0x3D,0x69,0xD8,0x47,0x7D,0xE4
    };
    memcpy(out, g, 16);
}
static int parse_type_guid(const char *s, uint8_t out[16]) {
    if (!s) return 0;
    if (strcasecmp(s, "linuxfs") == 0 || strcasecmp(s, "linux") == 0) {
        guid_linuxfs(out);
        return 1;
    }
    // TODO: support raw GUID text later
    return 0;
}

// ---------- usage ----------
static void gpt_print_usage(void) {
    puts("gpt init  <img|/dev/X> [--sector 512] [--entries 128] [--entry-size 128]");
    puts("gpt add   <img|/dev/X> --type linuxfs --name NAME --start 1MiB --size 100%");
    puts("gpt print <img|/dev/X>");
}

// ---------- init ----------
static int handle_gpt_init(int argc, char **argv) {
    if (argc < 3) { gpt_print_usage(); return 2; }

    const char *arg = argv[2];
    const char *img = resolve_image_or_dev(arg);
    if (!img) {
        fprintf(stderr, "gpt init: cannot resolve \"%s\" (use -i <image> %s first)\n", arg, arg);
        return 1;
    }

    uint32_t sector_bytes = 512, num_entries = 128, entry_size = 128;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--sector") == 0 && i + 1 < argc)      sector_bytes = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc)    num_entries = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--entry-size") == 0 && i + 1 < argc) entry_size  = (uint32_t)strtoul(argv[++i], NULL, 0);
    }

    uint64_t sz = filesize_bytes(img);
    printf("GPT init: image=\"%s\" size=%llu bytes (%.2f MiB) sector=%u entries=%u entry-size=%u\n",
           img, (unsigned long long)sz, sz / 1048576.0, sector_bytes, num_entries, entry_size);

    int rc = gpt_init_fresh(img, sector_bytes, num_entries, entry_size);
    if (rc != 0) {
        fprintf(stderr, "gpt init: failed (rc=%d)\n", rc);
        return 1;
    }
    printf("GPT initialized: sector=%u entries=%u entry-size=%u\n", sector_bytes, num_entries, entry_size);
    return 0;
}

// ---------- add ----------
static int handle_gpt_add(int argc, char **argv) {
    // gpt add <img|/dev/X> --type linuxfs --name NAME --start 1MiB --size 100%
    if (argc < 3) { 
        puts("gpt add <img|/dev/X> --type <alias> --name NAME --start <off> --size <len|%>");
        return 2; 
    }
    const char *arg = argv[2];
    const char *img = resolve_image_or_dev(arg);
    if (!img) {
        fprintf(stderr, "gpt add: cannot resolve \"%s\" (use -i <image> %s first)\n", arg, arg);
        return 1;
    }

    const char *type_s=NULL, *name=NULL, *start_s=NULL, *size_s=NULL;
    for (int i = 3; i < argc; ++i) {
        if      (strcmp(argv[i], "--type") == 0 && i + 1 < argc) type_s  = argv[++i];
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) name    = argv[++i];
        else if (strcmp(argv[i], "--start")== 0 && i + 1 < argc) start_s = argv[++i];
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) size_s  = argv[++i];
    }
    if (!type_s || !name) { fprintf(stderr, "gpt add: --type and --name are required\n"); return 2; }

    uint8_t type_guid[16];
    if (!parse_type_guid(type_s, type_guid)) {
        fprintf(stderr, "gpt add: unknown type \"%s\" (supported: linuxfs)\n", type_s);
        return 2;
    }

    GptHeader hdr;
    if (!gpt_read_header(img, &hdr, 1)) {
        fprintf(stderr, "gpt add: failed to read GPT header from %s\n", img);
        return 1;
    }

    // We default to 512 here. If you later support 4K, thread that value in.
    uint32_t sector = 512;
    uint64_t first_usable = hdr.first_usable_lba;
    uint64_t last_usable  = hdr.last_usable_lba;

    uint64_t first_lba = first_usable;
    if (start_s && *start_s) {
        uint64_t off_bytes = parse_size_bytes(start_s);
        if (off_bytes == 0) { fprintf(stderr, "gpt add: invalid --start %s\n", start_s); return 2; }
        first_lba = off_bytes / sector;
        if (off_bytes % sector) first_lba++; // ceil
    }

    uint64_t last_lba = last_usable;
    if (size_s && *size_s) {
        int pct = parse_percent(size_s);
        if (pct >= 0) {
            uint64_t max_sectors = (last_usable >= first_lba) ? (last_usable - first_lba + 1) : 0;
            uint64_t use = (pct == 100) ? max_sectors : (uint64_t)((double)max_sectors * ((double)pct / 100.0) + 0.5);
            if (use == 0) { fprintf(stderr, "gpt add: zero size from %s\n", size_s); return 2; }
            last_lba = first_lba + use - 1;
        } else {
            uint64_t size_bytes = parse_size_bytes(size_s);
            if (size_bytes == 0) { fprintf(stderr, "gpt add: invalid --size %s\n", size_s); return 2; }
            uint64_t sectors = size_bytes / sector;
            if (size_bytes % sector) sectors++; // ceil
            if (sectors == 0) { fprintf(stderr, "gpt add: size too small\n"); return 2; }
            last_lba = first_lba + sectors - 1;
        }
    }

    if (first_lba < first_usable) first_lba = first_usable;
    if (last_lba  > last_usable ) last_lba  = last_usable;
    if (first_lba > last_lba) {
        fprintf(stderr, "gpt add: empty range after clamping (first=%llu last=%llu)\n",
            (unsigned long long)first_lba, (unsigned long long)last_lba);
        return 2;
    }

    int rc = gpt_add_partition_lba(img, type_guid, name, first_lba, last_lba);
    if (rc != 0) { fprintf(stderr, "gpt add: failed (rc=%d)\n", rc); return 1; }

    double sz_mb = ((double)(last_lba - first_lba + 1) * sector) / (1024.0*1024.0);
    printf("Added %s  start=%llu  end=%llu  size=%.1fMB  name=%s\n",
           type_s, (unsigned long long)first_lba, (unsigned long long)last_lba, sz_mb, name);
    return 0;
}

// ---------- print ----------
static int handle_gpt_print(int argc, char **argv) {
    if (argc < 3) { gpt_print_usage(); return 2; }
    const char *arg = argv[2];
    const char *img = resolve_image_or_dev(arg);
    if (!img) {
        fprintf(stderr, "gpt print: cannot resolve \"%s\" (use -i <image> %s first)\n", arg, arg);
        return 1;
    }

    GptHeader h;
    if (!gpt_read_header(img, &h, 1)) {
        fprintf(stderr, "gpt print: failed to read GPT header on %s\n", img);
        return 1;
    }

    // We don't store sector size in GPT, so assume 512 (your default)
    const uint32_t sector = 512;

    printf("Disk: %s  Sector: %u\n", img, sector);
    char guid_str[37]; gpt_guid_to_str(h.disk_guid, guid_str);
    printf("Disk GUID: %s\n", guid_str);
    printf("Primary GPT: LBA %llu | Array: LBA %llu  (entries=%u, size=%u)\n",
           (unsigned long long)h.current_lba,
           (unsigned long long)h.part_entry_lba,
           h.num_part_entries, h.part_entry_size);
    printf("Backup  GPT: LBA %llu\n\n", (unsigned long long)h.backup_lba);

    GptEntry *ents = NULL;
    if (!gpt_read_entries(img, &h, &ents)) {
        fprintf(stderr, "gpt print: failed to read entries\n");
        return 1;
    }

    printf("Idx  %-12s  %-12s  %-10s  %-10s  %s\n",
           "Start LBA", "End LBA", "Size", "Type", "Name");
    printf("---  %-12s  %-12s  %-10s  %-10s  %s\n",
           "------------", "------------", "----------", "----------", "----------------");

    for (uint32_t i = 0; i < h.num_part_entries; ++i) {
        const GptEntry *e = &ents[i];
        if (e->first_lba == 0 && e->last_lba == 0) continue;

        double sz_mb = ((double)(e->last_lba - e->first_lba + 1) * sector) / (1024.0*1024.0);
        const char *alias = gpt_alias_for_type(e->type_guid);
        char name_utf8[128] = {0};
		uint16_t name16[36];
		memcpy(name16, e->name_utf16, sizeof(name16));  // align away packed pointer
		gpt_utf16le_to_utf8(name16, 36, name_utf8, (int)sizeof(name_utf8));

        printf("%3u  %12llu  %12llu  %8.1fMB  %-10s %s\n",
               i+1,
               (unsigned long long)e->first_lba,
               (unsigned long long)e->last_lba,
               sz_mb,
               alias ? alias : "-",
               name_utf8);
    }
    free(ents);
    return 0;
}

// ---------- dispatcher ----------
int cmd_gpt(int argc, char **argv) {
    if (argc < 2) { gpt_print_usage(); return 2; }

    if (strcmp(argv[1], "init")  == 0) return handle_gpt_init(argc, argv);
    if (strcmp(argv[1], "add")   == 0) return handle_gpt_add(argc, argv);
    if (strcmp(argv[1], "print") == 0) return handle_gpt_print(argc, argv);

    gpt_print_usage();
    return 2;
}
