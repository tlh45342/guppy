#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#include "tar.h"

static uint64_t octal(const unsigned char *p, size_t n)
{
    uint64_t v = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0)) i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
        v = (v << 3) + (uint64_t)(p[i] - '0');
    return v;
}

static int zero(const unsigned char *p)
{
    size_t i;
    for (i = 0; i < 512; i++)
        if (p[i]) return 0;
    return 1;
}

int gu_tar_walk(FILE *fp, uint64_t off, uint64_t len, gu_tar_cb cb, void *ctx)
{
    uint64_t pos = 0;

    if (off > LONG_MAX || fseek(fp, (long)off, SEEK_SET)) return -1;

    while (pos + 512 <= len) {
        unsigned char h[512];
        gu_tar_entry_t e;
        uint64_t padded;

        if (fread(h, 1, 512, fp) != 512) return -2;
        pos += 512;
        if (zero(h)) return 0;

        memset(&e, 0, sizeof e);
        memcpy(e.path, h, 100);
        e.path[100] = 0;
        if (h[345]) {
            char pre[156];
            memcpy(pre, h + 345, 155);
            pre[155] = 0;
            snprintf(e.path, sizeof e.path, "%s/%.*s", pre, 100, (char *)h);
        }
        memcpy(e.linkname, h + 157, 100);
        e.linkname[100] = 0;
        e.mode = (unsigned)octal(h + 100, 8);
        e.size = octal(h + 124, 12);
        e.type = h[156] ? (char)h[156] : '0';
        e.data_offset = off + pos;

        if (e.size > len - pos) return -3;
        if (cb && cb(fp, &e, ctx)) return -4;

        padded = (e.size + 511u) & ~511u;
        if (padded > len - pos) return -5;
        pos += padded;
        if (off + pos > LONG_MAX || fseek(fp, (long)(off + pos), SEEK_SET))
            return -6;
    }
    return 0;
}

int gu_tar_walk_stream(FILE *fp, gu_tar_cb cb, void *ctx)
{
    long end;
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    end = ftell(fp);
    if (end < 0) return -1;
    return gu_tar_walk(fp, 0, (uint64_t)end, cb, ctx);
}
