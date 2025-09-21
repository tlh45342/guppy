#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int fseek64(FILE *f, uint64_t off);
int file_read_at (FILE *f, uint64_t off, void *buf, size_t n);
int file_write_at(FILE *f, uint64_t off, const void *buf, size_t n);

int file_read_at_path (const char *path, uint64_t off, void *buf,        size_t n);
int file_write_at_path(const char *path, uint64_t off, const void *buf,  size_t n);
int file_ensure_size  (const char *path, uint64_t size_bytes);