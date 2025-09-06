#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "fileutil.h"

// ----- portable 64-bit seek wrappers -----
static int fseek64(FILE* f, uint64_t off) {
#if defined(_WIN32) || defined(_WIN64)
    // MSVC / MinGW provide 64-bit seek as _fseeki64
    return _fseeki64(f, (long long)off, SEEK_SET);
#else
    // POSIX: use fseeko with off_t (typically 64-bit on LP64 / LFS)
    return fseeko(f, (off_t)off, SEEK_SET);
#endif
}

static uint64_t ftell64(FILE* f) {
#if defined(_WIN32) || defined(_WIN64)
    return (uint64_t)_ftelli64(f);
#else
    return (uint64_t)ftello(f);
#endif
}

// Ensure file exists and is exactly `size` bytes.
// We extend by seeking to size-1 and writing a zero byte.
int file_ensure_size(const char* path, uint64_t size) {
    FILE* f = fopen(path, "rb+");
    if (!f) {
        f = fopen(path, "wb+");
        if (!f) return -1;
    }

    // If size == 0, just truncate by reopening "wb+".
    if (size == 0) {
        fclose(f);
        f = fopen(path, "wb+");
        if (!f) return -1;
        fclose(f);
        return 0;
    }

    if (fseek64(f, size - 1) != 0) { fclose(f); return -1; }
    unsigned char z = 0;
    size_t n = fwrite(&z, 1, 1, f);
    if (n != 1) { fclose(f); return -1; }

    fflush(f);
#if !defined(_WIN32) && !defined(_WIN64)
    // Best effort flush to disk on POSIX
    // (on Windows, fflush is typically enough here for simple tooling)
    // fsync(fileno(f));  // optional; add <unistd.h> if you enable this
#endif
    fclose(f);
    return 0;
}

int file_read_at(void* buf, size_t bytes, uint64_t off, const char* path) {
    if (bytes == 0) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek64(f, off) != 0) { fclose(f); return -1; }
    size_t n = fread(buf, 1, bytes, f);
    fclose(f);
    return (n == bytes) ? 0 : -1;
}

int file_write_at(const void* buf, size_t bytes, uint64_t off, const char* path) {
    if (bytes == 0) return 0;
    FILE* f = fopen(path, "rb+");
    if (!f) {
        f = fopen(path, "wb+");
        if (!f) return -1;
    }
    if (fseek64(f, off) != 0) { fclose(f); return -1; }
    size_t n = fwrite(buf, 1, bytes, f);
    fflush(f);
    fclose(f);
    return (n == bytes) ? 0 : -1;
}
