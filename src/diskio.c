#include "diskio.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ========================= existing file_* I/O ========================= */

bool file_pread(void *buf, size_t n, size_t off, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, (long)off, SEEK_SET) != 0) { fclose(f); return false; }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got == n;
}

bool file_pwrite(const void *buf, size_t n, size_t off, const char *path) {
    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "w+b");
    if (!f) return false;
    if (fseek(f, (long)off, SEEK_SET) != 0) { fclose(f); return false; }
    size_t put = fwrite(buf, 1, n, f);
    fclose(f);
    return put == n;
}

uint64_t filesize_bytes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

/* ====================== devkey -> path mapping (shim) ======================= */

#ifndef DISKIO_MAX_MAP
#define DISKIO_MAX_MAP 64
#endif

#ifndef DISKIO_PATH_MAX
#define DISKIO_PATH_MAX 512
#endif

typedef struct {
    char key[32];               /* devkey, e.g., "/dev/a" */
    char path[DISKIO_PATH_MAX]; /* backing file path */
} diskio_map_entry_t;

static diskio_map_entry_t g_map[DISKIO_MAX_MAP];
static int g_map_count = 0;

static int map_find_index(const char *devkey) {
    for (int i = 0; i < g_map_count; ++i)
        if (strcmp(g_map[i].key, devkey) == 0) return i;
    return -1;
}

static int is_devkey(const char *s) {
    return s && s[0]=='/' && s[1]=='d' && s[2]=='e' && s[3]=='v' && s[4]=='/';
}

bool diskio_attach_image(const char *devkey, const char *path, uint64_t *bytes_out) {
    if (!devkey || !*devkey || !path || !*path) return false;

    /* Verify the file exists and is readable */
    uint64_t sz = filesize_bytes(path);
    if (sz == 0) {
        /* Zero-size images are unlikely here; treat as failure for safety. */
        return false;
    }

    int idx = map_find_index(devkey);
    if (idx < 0) {
        if (g_map_count >= DISKIO_MAX_MAP) return false;
        idx = g_map_count++;
    }
    snprintf(g_map[idx].key,  sizeof g_map[idx].key,  "%.*s",  (int)sizeof g_map[idx].key  - 1, devkey);
    snprintf(g_map[idx].path, sizeof g_map[idx].path, "%.*s",  (int)sizeof g_map[idx].path - 1, path);

    if (bytes_out) *bytes_out = sz;
    return true;
}

bool diskio_detach(const char *devkey) {
    int idx = map_find_index(devkey);
    if (idx < 0) return false;
    for (int i = idx + 1; i < g_map_count; ++i) g_map[i-1] = g_map[i];
    --g_map_count;
    return true;
}

const char *diskio_resolve(const char *devkey) {
    int idx = map_find_index(devkey);
    if (idx >= 0) return g_map[idx].path;

    /* SAFETY: never treat /dev/ as a host path */
    if (is_devkey(devkey)) return NULL;

    /* Allow raw filesystem paths used as keys */
    return (devkey && *devkey) ? devkey : NULL;
}

bool diskio_pread(const char *devkey, uint64_t off, void *dst, uint32_t len) {
    if (!dst) return false;
    const char *path = diskio_resolve(devkey);
    if (!path) {
        fprintf(stderr, "diskio_pread: unmapped devkey '%s'\n", devkey ? devkey : "(null)");
        return false;
    }
    return file_pread(dst, (size_t)len, (size_t)off, path);
}

bool diskio_pwrite(const char *devkey, uint64_t off, const void *src, uint32_t len) {
    if (!src) return false;
    const char *path = diskio_resolve(devkey);
    if (!path) {
        fprintf(stderr, "diskio_pwrite: unmapped devkey '%s'\n", devkey ? devkey : "(null)");
        return false;
    }
    return file_pwrite(src, (size_t)len, (size_t)off, path);
}

uint64_t diskio_size_bytes(const char *devkey) {
    const char *path = diskio_resolve(devkey);
    if (!path) return 0;
    return filesize_bytes(path);
}