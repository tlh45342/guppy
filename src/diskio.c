#include "diskio.h"
#include <stdio.h>
#include <sys/stat.h>

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
    if (!f) f = fopen(path, "w+b");  // create if missing
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
