#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
    char     signature[8];     // "EFI PART"
    uint32_t revision;         // 00 00 01 00
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t part_entry_lba;
    uint32_t num_part_entries;
    uint32_t part_entry_size;  // typically 128
    uint32_t part_array_crc32;
    // rest of sector is reserved/padding
} GptHeader;

typedef struct __attribute__((packed)) {
    uint8_t  type_guid[16];
    uint8_t  uniq_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name_utf16[36]; // UTF-16LE, not null-terminated necessarily
} GptEntry;

bool gpt_read_header(const char *img, GptHeader *out, uint32_t sector);
bool gpt_read_entries(const char *img, const GptHeader *h, GptEntry **out_entries);
const char *gpt_alias_for_type(const uint8_t type_guid[16]);
void gpt_guid_to_str(const uint8_t g[16], char out[37]); // XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
void gpt_utf16le_to_utf8(const uint16_t *u16, int max_units, char *out, int outsz);

int gpt_init_fresh(const char *path,
                   uint32_t sector_bytes,        // 512 or 4096
                   uint32_t num_entries,         // e.g., 128
                   uint32_t entry_size_bytes);   // 128

// Append/insert one entry and rewrite headers+arrays (primary + backup).
// name_utf8 may be NULL/empty. type_guid is a 16-byte GPT type GUID.
int gpt_add_partition_lba(const char *path,
                          const uint8_t type_guid[16],
                          const char *name_utf8,
                          uint64_t first_lba,
                          uint64_t last_lba);

// return true if partition found; fills start_lba and total_sectors
bool gpt_get_partition(const char *image_path, int part_index,
                       uint64_t *start_lba, uint64_t *total_sectors);
					   
int gpt_find_single_partition(const char *image_path);