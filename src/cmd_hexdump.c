// src/cmd_hexdump.c — bounded hexadecimal dump through the VFS
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vfs.h"

#define HEXDUMP_DEFAULT_LEN 256u
#define HEXDUMP_MAX_LEN     (1024u * 1024u)

static void usage(void) {
    fprintf(stderr, "usage: hexdump [-n bytes] [-s offset] <path>\n");
}

static int parse_u64(const char *s, uint64_t *out) {
    char *end = NULL;
    unsigned long long v;
    if (!s || !*s || !out) return -1;
    errno = 0;
    v = strtoull(s, &end, 0);
    if (errno || !end || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static void print_line(uint64_t off, const uint8_t *buf, size_t n) {
    printf("%08" PRIx64 "  ", off);
    for (size_t i = 0; i < 16; ++i) {
        if (i < n) printf("%02x ", buf[i]);
        else       printf("   ");
        if (i == 7) putchar(' ');
    }
    printf(" |");
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = buf[i];
        putchar((c >= 32 && c <= 126) ? c : '.');
    }
    for (size_t i = n; i < 16; ++i) putchar(' ');
    puts("|");
}

int cmd_hexdump(int argc, char **argv) {
    uint64_t start = 0;
    uint64_t want = HEXDUMP_DEFAULT_LEN;
    const char *path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-n") == 0) {
            if (++i >= argc || parse_u64(argv[i], &want) != 0) { usage(); return 1; }
        } else if (strcmp(argv[i], "-s") == 0) {
            if (++i >= argc || parse_u64(argv[i], &start) != 0) { usage(); return 1; }
        } else if (argv[i][0] == '-') {
            usage(); return 1;
        } else if (!path) {
            path = argv[i];
        } else {
            usage(); return 1;
        }
    }

    if (!path) { usage(); return 1; }
    if (want > HEXDUMP_MAX_LEN) {
        fprintf(stderr, "hexdump: refusing more than %u bytes; use repeated bounded dumps\n",
                (unsigned)HEXDUMP_MAX_LEN);
        return 1;
    }

    struct file *f = NULL;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0 || !f) {
        fprintf(stderr, "hexdump: cannot open '%s'\n", path);
        return 1;
    }

    uint8_t scratch[4096];
    uint64_t skipped = 0;
    while (skipped < start) {
        size_t chunk = (size_t)((start - skipped) > sizeof scratch ? sizeof scratch : (start - skipped));
        ssize_t n = vfs_read(f, scratch, chunk);
        if (n < 0) {
            fprintf(stderr, "hexdump: read error on '%s'\n", path);
            vfs_close(f);
            return 1;
        }
        if (n == 0) break;
        skipped += (uint64_t)n;
    }

    if (skipped < start) {
        vfs_close(f);
        return 0;
    }

    uint64_t done = 0;
    while (done < want) {
        uint8_t line[16];
        size_t need = (size_t)((want - done) > sizeof line ? sizeof line : (want - done));
        ssize_t n = vfs_read(f, line, need);
        if (n < 0) {
            fprintf(stderr, "hexdump: read error on '%s'\n", path);
            vfs_close(f);
            return 1;
        }
        if (n == 0) break;
        print_line(start + done, line, (size_t)n);
        done += (uint64_t)n;
    }

    if (vfs_close(f) != 0) {
        fprintf(stderr, "hexdump: close error on '%s'\n", path);
        return 1;
    }
    return 0;
}
