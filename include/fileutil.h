#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdint.h>
#include <stdbool.h>

// Ensure a file exists and is exactly `size` bytes (zero-filled at end if needed).
int file_ensure_size(const char* path, uint64_t size);

// Read `bytes` from absolute offset `off` in file `path` into `buf`.
// Returns 0 on success, -1 on error/short read.
int file_read_at(void* buf, size_t bytes, uint64_t off, const char* path);

// Write `bytes` from `buf` to absolute offset `off` in file `path`.
// Creates the file if it doesn't exist. Returns 0 on success, -1 on error/short write.
int file_write_at(const void* buf, size_t bytes, uint64_t off, const char* path);

// Small helpers (optional)
bool fread_exact(void *dst, size_t len, FILE *f);
bool fwrite_exact(const void *src, size_t len, FILE *f);