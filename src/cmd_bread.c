// cmd_bread.c - read logical blocks from a registered block device/partition.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <ctype.h>
#include <limits.h>

#include "vblk.h"
#include "app_printf.h"

#define BREAD_DEFAULT_COUNT 1u
#define BREAD_MAX_COUNT     128u

static int parse_u64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v;

    if (!s || !*s || *s == '-') return 0;
    errno = 0;
    v = strtoull(s, &end, 0);
    if (errno == ERANGE || !end || *end != '\0') return 0;
    *out = (uint64_t)v;
    return 1;
}

static void dump_line(uint64_t off, const uint8_t *p, size_t n)
{
    app_printf("%08" PRIx64 "  ", off);
    for (size_t i = 0; i < 16; ++i) {
        if (i < n) app_printf("%02x ", (unsigned)p[i]);
        else app_printf("   ");
        if (i == 7) app_printf(" ");
    }
    app_printf(" | ");
    for (size_t i = 0; i < n; ++i)
        app_printf("%c", isprint((unsigned char)p[i]) ? (char)p[i] : '.');
    app_printf("\n");
}

int cmd_bread(int argc, char **argv)
{
    uint64_t lba, count64 = BREAD_DEFAULT_COUNT;
    uint32_t count;
    uint32_t bsz;
    uint64_t bytes64;
    uint64_t dev_bytes;
    uint64_t start;
    uint8_t *buf;
    vblk_t *vb;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: bread <device> <lba> [count]\n");
        return 1;
    }
    if (!parse_u64(argv[2], &lba)) {
        fprintf(stderr, "bread: invalid LBA '%s'\n", argv[2]);
        return 1;
    }
    if (argc == 4 && (!parse_u64(argv[3], &count64) || count64 == 0 || count64 > BREAD_MAX_COUNT)) {
        fprintf(stderr, "bread: count must be between 1 and %u\n", BREAD_MAX_COUNT);
        return 1;
    }
    count = (uint32_t)count64;

    vb = vblk_open(argv[1]);
    if (!vb) {
        fprintf(stderr, "bread: cannot open device '%s'\n", argv[1]);
        return 1;
    }
    bsz = vb->block_bytes ? vb->block_bytes : 512u;
    if (lba > UINT64_MAX / (uint64_t)bsz) {
        fprintf(stderr, "bread: LBA is out of range\n");
        return 1;
    }
    start = lba * (uint64_t)bsz;
    bytes64 = (uint64_t)count * (uint64_t)bsz;
    dev_bytes = vblk_size_bytes(vb);
    if (start > dev_bytes || bytes64 > dev_bytes - start) {
        fprintf(stderr, "bread: request exceeds device '%s' (%" PRIu64 " bytes)\n", argv[1], dev_bytes);
        return 1;
    }
    if (bytes64 > SIZE_MAX) {
        fprintf(stderr, "bread: request is too large for this host\n");
        return 1;
    }

    buf = (uint8_t *)malloc((size_t)bytes64);
    if (!buf) {
        fprintf(stderr, "bread: out of memory\n");
        return 1;
    }
    if (!vblk_read_blocks(vb, lba, count, buf)) {
        fprintf(stderr, "bread: read failed on %s at LBA %" PRIu64 " count %u\n", argv[1], lba, count);
        free(buf);
        return 1;
    }

    for (uint32_t s = 0; s < count; ++s) {
        const uint8_t *sector = buf + (size_t)s * bsz;
        app_printf("LBA %" PRIu64 " (%u bytes)\n", lba + s, bsz);
        for (uint32_t off = 0; off < bsz; off += 16) {
            size_t n = bsz - off;
            if (n > 16) n = 16;
            dump_line((uint64_t)s * bsz + off, sector + off, n);
        }
        if (s + 1 < count) app_printf("\n");
    }

    free(buf);
    return 0;
}
