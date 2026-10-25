/*
 * ar_write.c
 *
 * Write the traditional ar archive format used as the outer container
 * for Debian packages. Member sizes and names must fit fixed-width fields.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ar_write.h"

/* Write the eight-byte archive signature. */
int gu_ar_write_begin(FILE *out)
{
    if (out == NULL || fwrite("!<arch>\n", 1, 8, out) != 8) {
        return -1;
    }
    return 0;
}

/* Copy a value into a space-padded, fixed-width archive field. */
static int field(char *dst, size_t width, const char *value)
{
    size_t length = strlen(value);

    if (length > width) {
        return -1;
    }
    memset(dst, ' ', width);
    memcpy(dst, value, length);
    return 0;
}

/*
 * Append one archive member, copying exactly size bytes from in.
 * Odd-length members require one newline byte of alignment padding.
 */
int gu_ar_write_member(FILE *out, const char *name, FILE *in,
                       uint64_t size, unsigned mode)
{
    char header[60];
    char buffer[32768];
    char number[48];
    char member_name[18];
    size_t count;
    size_t name_length;
    uint64_t remaining = size;

    if (out == NULL || name == NULL || in == NULL) {
        return -1;
    }

    name_length = strlen(name);
    if (name_length == 0 || name_length > 15 ||
        strchr(name, '/') != NULL || strchr(name, '\n') != NULL ||
        strchr(name, ' ') != NULL) {
        return -1;
    }

    snprintf(member_name, sizeof member_name, "%s/", name);
    memset(header, ' ', sizeof header);

    if (field(header, 16, member_name) != 0 ||
        field(header + 16, 12, "0") != 0 ||
        field(header + 28, 6, "0") != 0 ||
        field(header + 34, 6, "0") != 0) {
        return -1;
    }

    snprintf(number, sizeof number, "%o", mode & 07777u);
    if (field(header + 40, 8, number) != 0) {
        return -1;
    }

    snprintf(number, sizeof number, "%" PRIu64, size);
    if (field(header + 48, 10, number) != 0) {
        return -1;
    }

    header[58] = '`';
    header[59] = '\n';
    if (fwrite(header, 1, sizeof header, out) != sizeof header) {
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

    if ((size & 1u) != 0 && fputc('\n', out) == EOF) {
        return -1;
    }
    return 0;
}
