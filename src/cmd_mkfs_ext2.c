// src/cmd_mkfs_ext2.c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "vblk.h"
#include "diskio.h"

/* --------------------------------------------------------------------------
   Formatter core compatibility layer.
   We don't know which symbol your mkfs core exposes, so declare a few
   common candidates as WEAK. GCC/Clang will allow them to be NULL if
   not provided, avoiding link errors. On MSVC you'd pick one and link that.
   -------------------------------------------------------------------------- */
#if defined(__GNUC__) || defined(__clang__)
  #define WEAK __attribute__((weak))
#else
  #define WEAK
#endif

/* Most capable: accepts base key, byte offset, total bytes, and label. */
extern int mkfs_ext2_core(const char *key, uint64_t off, uint64_t bytes, const char *label) WEAK;
/* Alternate name some trees use, same signature as above. */
extern int mkfs_ext2_make_at(const char *key, uint64_t off, uint64_t bytes, const char *label) WEAK;
/* Older/simple signature: whole-device only (no offset). */
extern int mkfs_ext2_make(const char *key, uint64_t bytes, const char *label) WEAK;

static int call_ext2_mkfs(const char *key, uint64_t off, uint64_t bytes, const char *label) {
#if defined(__GNUC__) || defined(__clang__)
    if (mkfs_ext2_core)     return mkfs_ext2_core(key, off, bytes, label);
    if (mkfs_ext2_make_at)  return mkfs_ext2_make_at(key, off, bytes, label);
    if (mkfs_ext2_make) {
        if (off != 0) {
            fprintf(stderr, "mkfs.ext2: this build's formatter doesn't support nonzero offsets\n");
            return -1;
        }
        return mkfs_ext2_make(key, bytes, label);
    }
    fprintf(stderr, "mkfs.ext2: no formatter core linked (mkfs_ext2_core/make_at/make missing)\n");
    return -1;
#else
    /* Non-GNU toolchains: pick the symbol you actually have. */
    return mkfs_ext2_core(key, off, bytes, label);
#endif
}

/* -------------------------------------------------------------------------- */

static void usage(void){
    printf("mkfs.ext2 <device> [--label NAME]\n");
}

int cmd_mkfs_ext2(int argc, char **argv){
    if (argc < 2) { usage(); return 0; }

    const char *target = argv[1];
    const char *label  = NULL;

    for (int i=2; i<argc; ++i){
        if (strcmp(argv[i], "--label")==0 && i+1<argc) {
            label = argv[++i];
        } else {
            fprintf(stderr, "mkfs.ext2: unknown option '%s'\n", argv[i]);
            return 0;
        }
    }

    char key[256];
    uint64_t off=0, len=0;

    /* Prefer vblk rows (so /dev/a1 works and maps to base key + slice offset). */
    if (vblk_resolve_to_base(target, key, sizeof key, &off, &len)) {
        /* ok */
    } else {
        /* Fall back to raw path/key (whole-disk formatting at offset 0). */
        const char *p = diskio_resolve(target);
        if (!p) {
            fprintf(stderr, "mkfs.ext2: unknown device %s (use -i <img> %s first)\n", target, target);
            return 0;
        }
        snprintf(key, sizeof key, "%s", p);
        off = 0;
        len = diskio_size_bytes(key);
    }

    if (len == 0) {
        fprintf(stderr, "mkfs.ext2: cannot determine size for %s\n", target);
        return 0;
    }

    /* NOTE: Pass uint64_t directly with PRIu64 (no casts) */
    printf("mkfs.ext2: formatting %s (key=%s, off=%" PRIu64 ", size=%" PRIu64 " bytes)%s%s\n",
           target, key, off, len, label ? " label=" : "", label ? label : "");

    int rc = call_ext2_mkfs(key, off, len, label ? label : "");
    if (rc != 0) {
        fprintf(stderr, "mkfs.ext2: failed (rc=%d)\n", rc);
        return 0;
    }
    printf("mkfs.ext2: done\n");
    return 0;
}
