#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Portable file helpers
bool file_pread(void *buf, size_t n, size_t off, const char *path);
bool file_pwrite(const void *buf, size_t n, size_t off, const char *path);
uint64_t filesize_bytes(const char *path);