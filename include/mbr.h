#pragma once
#include <stdint.h>

// Initialize an empty MBR sector (all zeros, 0x55AA signature).
int mbr_init_empty(const char* img_path);

// Read 512-byte MBR into out[512].
int mbr_read(unsigned char out[512], const char* img_path);

// Write 512-byte MBR from buffer to disk image.
int mbr_write(const unsigned char mbr[512], const char* img_path);

// Add a partition entry to the MBR.
// index = 1..4 (partition table slots).
// type = partition type byte (e.g. 0x0C for FAT32 LBA).
// start_bytes and size_bytes are in absolute bytes, not sectors.
// Caller is responsible for ensuring alignment to 512B sectors.
int mbr_add_partition(const char* img_path, int index, uint8_t type,
                      uint64_t start_bytes, uint64_t size_bytes);

// Print MBR partition table to stdout (basic diagnostic).
int mbr_print(const char* img_path);
