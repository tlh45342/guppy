/*
 * tar_write.c
 *
 * Write portable ustar headers and file data for Debian package members.
 * All sizes and header offsets follow the POSIX 512-byte TAR record layout.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tar_write.h"

#define TAR_BLOCK_SIZE 512

/* Encode an unsigned value as a NUL-terminated, zero-padded octal field. */
static int oct(unsigned char *dst, size_t width, uint64_t value)
{
    size_t i;
    uint64_t check = value;

    for (i = 0; i < width - 1; i++) {
        check >>= 3;
    }
    if (check != 0) {
        return -1;
    }

    memset(dst, '0', width);
    dst[width - 1] = 0;
    for (i = width - 1; i > 0 && value != 0; i--) {
        dst[i - 1] = (unsigned char)('0' + (value & 7));
        value >>= 3;
    }
    return 0;
}

/*
 * Split long relative paths across the ustar name and prefix fields.
 * Reject absolute paths and parent-directory traversal.
 */
static int name_fields(unsigned char *header, const char *path)
{
    size_t length;
    const char *slash = NULL;
    const char *cursor;

    if (path == NULL || *path == 0 || path[0] == '/' ||
        strchr(path, '\\') != NULL) {
        return -1;
    }
    if (strstr(path, "../") != NULL || strcmp(path, "..") == 0 ||
        strstr(path, "/./") != NULL || strcmp(path, ".") == 0) {
        return -1;
    }

    length = strlen(path);
    if (length <= 100) {
        memcpy(header, path, length);
        return 0;
    }

    for (cursor = path; cursor < path + length; cursor++) {
        size_t prefix_length = (size_t)(cursor - path);
        if (*cursor == '/' && prefix_length <= 155 &&
            length - prefix_length - 1 <= 100) {
            slash = cursor;
        }
    }

    if (slash == NULL || slash == path || slash[1] == 0) {
        return -1;
    }

    memcpy(header, slash + 1, length - (size_t)(slash - path) - 1);
    memcpy(header + 345, path, (size_t)(slash - path));
    return 0;
}

/* Construct the header, including the checksum with blank checksum bytes. */
static int header(FILE *out, const char *path, uint64_t size,
                  unsigned mode, char type)
{
    unsigned char block[TAR_BLOCK_SIZE];
    unsigned checksum = 0;
    size_t i;

    memset(block, 0, sizeof block);
    if (name_fields(block, path) != 0 ||
        oct(block + 100, 8, mode & 07777u) != 0 ||
        oct(block + 108, 8, 0) != 0 ||
        oct(block + 116, 8, 0) != 0 ||
        oct(block + 124, 12, size) != 0 ||
        oct(block + 136, 12, 0) != 0) {
        return -1;
    }

    memset(block + 148, ' ', 8);
    block[156] = (unsigned char)type;
    memcpy(block + 257, "ustar", 5);
    block[262] = 0;
    memcpy(block + 263, "00", 2);

    for (i = 0; i < sizeof block; i++) {
        checksum += block[i];
    }
    if (oct(block + 148, 7, checksum) != 0) {
        return -1;
    }
    block[155] = ' ';

    return fwrite(block, 1, sizeof block, out) == sizeof block ? 0 : -1;
}

/* Write one regular file followed by zero padding to a TAR block boundary. */
int gu_tar_write_file(FILE *out, const char *path, FILE *in,
                      uint64_t size, unsigned mode)
{
    unsigned char buffer[32768] = {0};
    uint64_t remaining = size;
    size_t count;
    size_t padding;

    if (out == NULL || in == NULL ||
        header(out, path, size, mode, '0') != 0) {
        return -1;
    }

    while (remaining != 0) {
        count = remaining < sizeof buffer ? (size_t)remaining : sizeof buffer;
        if (fread(buffer, 1, count, in) != count ||
            fwrite(buffer, 1, count, out) != count) {
            return -1;
        }
        remaining -= count;
    }

    padding = (size_t)((TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) %
                       TAR_BLOCK_SIZE);
    memset(buffer, 0, TAR_BLOCK_SIZE);
    if (padding != 0 && fwrite(buffer, 1, padding, out) != padding) {
        return -1;
    }
    return 0;
}

/* Append a directory entry, ensuring its archive name ends in '/'. */
int gu_tar_write_dir(FILE *out, const char *path, unsigned mode)
{
    char name[256];
    size_t length;

    if (out == NULL || path == NULL) {
        return -1;
    }

    length = strlen(path);
    if (length == 0 || length >= sizeof name - 1) {
        return -1;
    }

    memcpy(name, path, length);
    if (name[length - 1] != '/') {
        name[length++] = '/';
    }
    name[length] = 0;
    return header(out, name, 0, mode, '5');
}

/* Two all-zero records mark the end of a TAR archive. */
int gu_tar_write_end(FILE *out)
{
    unsigned char zeros[TAR_BLOCK_SIZE * 2] = {0};

    if (out == NULL || fwrite(zeros, 1, sizeof zeros, out) != sizeof zeros) {
        return -1;
    }
    return 0;
}
