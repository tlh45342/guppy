// src/fileutil.c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

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

static int seek64(FILE *f, uint64_t off) {
    return fseeko(f, (off_t)off, SEEK_SET);
}

int file_read_at_path(const char *path, uint64_t off, void *buf, size_t n) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (seek64(fp, off) != 0) { fclose(fp); return -1; }
    size_t got = fread(buf, 1, n, fp);
    fclose(fp);
    return (got == n) ? 0 : -1;
}

/* THIS WAS THE CONFLICT — it should be WRITE, not another READ */
int file_write_at_path(const char *path, uint64_t off, const void *buf, size_t n) {
    FILE *fp = fopen(path, "r+b");
    if (!fp) {
        // try create
        fp = fopen(path, "w+b");
        if (!fp) return -1;
    }
    if (seek64(fp, off) != 0) { fclose(fp); return -1; }
    size_t put = fwrite(buf, 1, n, fp);
    fflush(fp);
    fclose(fp);
    return (put == n) ? 0 : -1;
}

int file_ensure_size(const char *path, uint64_t size_bytes) {
    FILE *fp = fopen(path, "r+b");
    if (!fp) {
        fp = fopen(path, "w+b");
        if (!fp) return -1;
    }
    if (seek64(fp, size_bytes ? size_bytes - 1 : 0) != 0) { fclose(fp); return -1; }
    if (size_bytes) {
        unsigned char zero = 0;
        if (fwrite(&zero, 1, 1, fp) != 1) { fclose(fp); return -1; }
    }
    fflush(fp);
    fclose(fp);
    return 0;
}