#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct gu_tar_entry {
    char path[512];
    char linkname[256];
    uint64_t size;
    unsigned mode;
    char type;
    uint64_t data_offset;
} gu_tar_entry_t;

typedef int (*gu_tar_cb)(FILE *fp, const gu_tar_entry_t *e, void *ctx);
int gu_tar_walk(FILE *fp, uint64_t start, uint64_t length, gu_tar_cb cb, void *ctx);

/* Walk a complete seekable tar stream from byte zero. */
int gu_tar_walk_stream(FILE *fp, gu_tar_cb cb, void *ctx);
