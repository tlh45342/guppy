#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "gu_xz.h"
#include "xz.h"

static void seterr(char *b, size_t n, const char *s)
{
    if (b && n) snprintf(b, n, "%s", s);
}

static const char *xzerr(enum xz_ret r)
{
    switch (r) {
    case XZ_OK: return "decoder needs more input/output";
    case XZ_STREAM_END: return "success";
    case XZ_UNSUPPORTED_CHECK: return "unsupported integrity check";
    case XZ_MEM_ERROR: return "out of memory";
    case XZ_MEMLIMIT_ERROR: return "XZ dictionary exceeds decoder memory limit";
    case XZ_FORMAT_ERROR: return "not an XZ stream";
    case XZ_OPTIONS_ERROR: return "unsupported XZ options";
    case XZ_DATA_ERROR: return "corrupt XZ data";
    case XZ_BUF_ERROR: return "truncated XZ data or no decoder progress";
    default: return "XZ decode error";
    }
}

FILE *gu_xz_decode_member(FILE *src, uint64_t off, uint64_t len,
                          char *errbuf, size_t errbuf_sz)
{
    unsigned char inbuf[32768];
    unsigned char outbuf[32768];
    uint64_t left = len;
    struct xz_dec *dec = NULL;
    struct xz_buf b;
    enum xz_ret ret = XZ_OK;
    FILE *out = NULL;
    int eof_input = 0;

    if (!src || off > LONG_MAX || fseek(src, (long)off, SEEK_SET) != 0) {
        seterr(errbuf, errbuf_sz, "cannot seek to XZ member");
        return NULL;
    }

    /*
     * Debian packages are trusted build input in our current use, but still
     * cap the LZMA2 dictionary at 256 MiB so malformed input cannot request
     * essentially unlimited memory.
     */
    xz_crc32_init();
    xz_crc64_init();
    dec = xz_dec_init(XZ_DYNALLOC, 256U * 1024U * 1024U);
    if (!dec) {
        seterr(errbuf, errbuf_sz, "cannot allocate XZ decoder");
        return NULL;
    }

    out = tmpfile();
    if (!out) {
        xz_dec_end(dec);
        seterr(errbuf, errbuf_sz, "cannot create temporary TAR stream");
        return NULL;
    }

    memset(&b, 0, sizeof b);
    b.in = inbuf;
    b.out = outbuf;

    for (;;) {
        if (b.in_pos == b.in_size && !eof_input) {
            size_t want = left < sizeof inbuf ? (size_t)left : sizeof inbuf;
            size_t got = want ? fread(inbuf, 1, want, src) : 0;
            if (got != want) {
                seterr(errbuf, errbuf_sz, "short read in XZ member");
                goto fail;
            }
            left -= got;
            b.in_pos = 0;
            b.in_size = got;
            if (left == 0)
                eof_input = 1;
        }

        b.out_pos = 0;
        b.out_size = sizeof outbuf;

        ret = xz_dec_run(dec, &b);

        if (b.out_pos &&
            fwrite(outbuf, 1, b.out_pos, out) != b.out_pos) {
            seterr(errbuf, errbuf_sz, "cannot write temporary TAR stream");
            goto fail;
        }

        if (ret == XZ_STREAM_END)
            break;

        if (ret != XZ_OK) {
            seterr(errbuf, errbuf_sz, xzerr(ret));
            goto fail;
        }

        /*
         * With all member bytes supplied, XZ Embedded will eventually return
         * STREAM_END or an error. Avoid spinning if neither input nor output
         * can make progress.
         */
        if (eof_input && b.in_pos == b.in_size && b.out_pos == 0) {
            seterr(errbuf, errbuf_sz, "truncated XZ stream");
            goto fail;
        }
    }

    xz_dec_end(dec);

    if (fflush(out) != 0 || fseek(out, 0, SEEK_SET) != 0) {
        seterr(errbuf, errbuf_sz, "cannot rewind temporary TAR stream");
        fclose(out);
        return NULL;
    }

    return out;

fail:
    xz_dec_end(dec);
    fclose(out);
    return NULL;
}
