#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Low-level file I/O (existing in your tree) */
bool     file_pread (void *buf, size_t n, size_t off, const char *path);
bool     file_pwrite(const void *buf, size_t n, size_t off, const char *path);
uint64_t filesize_bytes(const char *path);

/* Devkey ⇄ path mapping + safe block I/O */
bool        diskio_attach_image(const char *devkey, const char *path, uint64_t *bytes_out);
bool        diskio_detach      (const char *devkey);
const char *diskio_resolve     (const char *devkey); 

bool diskio_pread (const char *devkey, uint64_t off, void *dst, uint32_t len);
bool diskio_pwrite(const char *devkey, uint64_t off, const void *src, uint32_t len);

uint64_t diskio_size_bytes(const char *devkey);
