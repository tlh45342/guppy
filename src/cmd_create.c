// src/cmd_disk.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// ---- externs provided elsewhere ----
// parse_size(...) is in guppy.c (or move to a shared util if you like)
extern uint64_t parse_size(const char *s, int *ok);
// file_ensure_size(...) is in fileutil.c
extern int file_ensure_size(const char *path, uint64_t size);

// ---- local helpers ----
static bool starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    size_t ls = strlen(s), lp = strlen(prefix);
    return (lp <= ls) && (strncmp(s, prefix, lp) == 0);
}

// Accepts "--size=256MiB" inline form
static uint64_t parse_size_arg_inline(const char *arg) {
    const char *eq = strchr(arg, '=');
    if (!eq || *(eq + 1) == '\0') return 0;
    int ok = 0;
    uint64_t v = parse_size(eq + 1, &ok);
    return ok ? v : 0;
}

// Minimal blank MBR writer (zeros + 0x55AA at offset 510)
static int mbr_write_blank(const char *img_path) {
    unsigned char mbr[512];
    memset(mbr, 0, sizeof(mbr));
    mbr[510] = 0x55;
    mbr[511] = 0xAA;

    FILE *f = fopen(img_path, "rb+");
    if (!f) f = fopen(img_path, "wb+");
    if (!f) { perror("mbr_write_blank/fopen"); return -1; }

    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    size_t n = fwrite(mbr, 1, sizeof(mbr), f);
    fflush(f);
    fclose(f);
    return (n == sizeof(mbr)) ? 0 : -1;
}

// ---- command: create ----
// Usage:
//   create <img> --size <N[KiB|MiB|GiB]> [--mbr]
//   create <img> --size=256MiB [--mbr]
int cmd_create(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "create: not enough arguments\n");
        fprintf(stderr, "usage: create <img> --size <N[KiB|MiB|GiB]> [--mbr]\n");
        return 2;
    }

    const char *img = argv[1];
    uint64_t size_bytes = 0;
    bool use_mbr = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "create: --size requires a value\n");
                return 2;
            }
            int ok = 0;
            size_bytes = parse_size(argv[++i], &ok);
            if (!ok || size_bytes == 0) {
                fprintf(stderr, "create: invalid --size value\n");
                return 2;
            }
        } else if (starts_with(argv[i], "--size=")) {
            uint64_t v = parse_size_arg_inline(argv[i]);
            if (v == 0) {
                fprintf(stderr, "create: invalid --size value\n");
                return 2;
            }
            size_bytes = v;
        } else if (strcmp(argv[i], "--mbr") == 0) {
            use_mbr = true;
        } else {
            fprintf(stderr, "create: unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    if (size_bytes == 0) {
        fprintf(stderr, "create: --size is required and must be > 0\n");
        return 2;
    }

    if (file_ensure_size(img, size_bytes) != 0) {
        perror("create/file_ensure_size");
        return 1;
    }

    if (use_mbr) {
        if (mbr_write_blank(img) != 0) {
            fprintf(stderr, "create: failed to write blank MBR\n");
            return 1;
        }
    }

    printf("Created %s (%llu bytes)%s\n",
           img, (unsigned long long)size_bytes, use_mbr ? " with MBR" : "");
    return 0;
}
