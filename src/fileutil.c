// src/fileutil.c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "fileutil.h"

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

/* Seek to a 64-bit file offset. Returns 0 on success, non-zero on failure. */
int fseek64(FILE *f, uint64_t off) {
#if defined(_WIN32) && !defined(__CYGWIN__)
    return _fseeki64(f, (long long)off, SEEK_SET);
#else
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

/* Ensure that the file is at least `size` bytes long.
   Creates or extends with zeros if needed. */
int file_ensure_size(const char *path, uint64_t size) {
    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f) return -1;

    if (fseek64(f, size - 1) != 0) {
        fclose(f);
        return -1;
    }

    unsigned char zero = 0;
    if (fwrite(&zero, 1, 1, f) != 1) {
        fclose(f);
        return -1;
    }

    fflush(f);
    fclose(f);
    return 0;
}

/* read exactly n bytes at absolute file offset 'off' */
int file_read_at(FILE *f, uint64_t off, void *buf, size_t n) {
    if (!f || !buf || n == 0) return -1;
    if (fseek64(f, off) != 0) return -1;

    unsigned char *p = (unsigned char *)buf;
    size_t remain = n;
    while (remain) {
        size_t got = fread(p, 1, remain, f);
        if (got == 0) {
            if (ferror(f)) return -1;
            /* EOF before full read */
            return -1;
        }
        p += got;
        remain -= got;
    }
    return 0;
}

/* write exactly n bytes at absolute file offset 'off' */
int file_write_at(FILE *f, uint64_t off, const void *buf, size_t n) {
    if (!f || !buf || n == 0) return -1;
    if (fseek64(f, off) != 0) return -1;

    const unsigned char *p = (const unsigned char *)buf;
    size_t remain = n;
    while (remain) {
        size_t put = fwrite(p, 1, remain, f);
        if (put == 0) return -1;
        p += put;
        remain -= put;
    }
    return 0;
}

int file_read_at_path(const char *path, uint64_t off, void *buf, size_t n) {
    if (!path || !buf) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int rc = file_read_at(f, off, buf, n);
    fclose(f);
    return rc;
}

int file_write_at_path(const char *path, uint64_t off, const void *buf, size_t n) {
    if (!path || !buf) return -1;
    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f) return -1;
    int rc = file_write_at(f, off, buf, n);
    fflush(f);
#if defined(_WIN32) && !defined(__CYGWIN__)
    _commit(_fileno(f)); /* best-effort */
#endif
    fclose(f);
    return rc;
}