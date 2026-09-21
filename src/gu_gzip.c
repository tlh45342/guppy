#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gu_gzip.h"

#define GZ_FTEXT    0x01u
#define GZ_FHCRC    0x02u
#define GZ_FEXTRA   0x04u
#define GZ_FNAME    0x08u
#define GZ_FCOMMENT 0x10u
#define GZ_RESERVED 0xE0u

#define MAXBITS   15
#define MAXLCODES 288
#define MAXDCODES 32
#define MAXCODES  (MAXLCODES + MAXDCODES)

struct bit_reader {
    const unsigned char *src;
    size_t len;
    size_t pos;
    unsigned bitbuf;
    unsigned bitcnt;
};

struct huffman {
    unsigned short count[MAXBITS + 1];
    unsigned short symbol[MAXCODES];
};

static void seterr(char *b, size_t n, const char *s)
{
    if (b && n) snprintf(b, n, "%s", s);
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int getbits(struct bit_reader *br, unsigned need, unsigned *out)
{
    unsigned val;
    while (br->bitcnt < need) {
        if (br->pos >= br->len) return -1;
        br->bitbuf |= (unsigned)br->src[br->pos++] << br->bitcnt;
        br->bitcnt += 8;
    }
    val = need ? (br->bitbuf & ((1u << need) - 1u)) : 0u;
    br->bitbuf >>= need;
    br->bitcnt -= need;
    *out = val;
    return 0;
}

static void byte_align(struct bit_reader *br)
{
    br->bitbuf = 0;
    br->bitcnt = 0;
}

/* Build canonical Huffman decoding tables. Returns <0 for oversubscribed. */
static int huff_build(struct huffman *h, const unsigned char *length, unsigned n)
{
    unsigned offs[MAXBITS + 1];
    unsigned i, len;
    int left = 1;

    memset(h, 0, sizeof *h);
    for (i = 0; i < n; i++) {
        if (length[i] > MAXBITS) return -1;
        h->count[length[i]]++;
    }

    if (h->count[0] == n) return 0;

    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }

    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];

    for (i = 0; i < n; i++)
        if (length[i]) h->symbol[offs[length[i]]++] = (unsigned short)i;

    return left;
}

static int huff_decode(struct bit_reader *br, const struct huffman *h, unsigned *sym)
{
    unsigned code = 0, first = 0, index = 0, len;

    for (len = 1; len <= MAXBITS; len++) {
        unsigned bit, count;
        if (getbits(br, 1, &bit) != 0) return -1;
        code |= bit;
        count = h->count[len];
        if (code >= first && code - first < count) {
            *sym = h->symbol[index + (code - first)];
            return 0;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static void fixed_tables(struct huffman *litlen, struct huffman *dist)
{
    unsigned char ll[MAXLCODES], dd[32];
    unsigned i;
    for (i = 0; i <= 143; i++) ll[i] = 8;
    for (; i <= 255; i++) ll[i] = 9;
    for (; i <= 279; i++) ll[i] = 7;
    for (; i < MAXLCODES; i++) ll[i] = 8;
    for (i = 0; i < 32; i++) dd[i] = 5;
    (void)huff_build(litlen, ll, MAXLCODES);
    (void)huff_build(dist, dd, 32);
}

static int dynamic_tables(struct bit_reader *br,
                          struct huffman *litlen, struct huffman *dist)
{
    static const unsigned char order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    unsigned char clen[19] = {0};
    unsigned char lens[MAXCODES];
    struct huffman ch;
    unsigned hlit, hdist, hclen, i, total;

    if (getbits(br, 5, &hlit) || getbits(br, 5, &hdist) || getbits(br, 4, &hclen))
        return -1;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 32) return -1;

    for (i = 0; i < hclen; i++) {
        unsigned v;
        if (getbits(br, 3, &v)) return -1;
        clen[order[i]] = (unsigned char)v;
    }
    if (huff_build(&ch, clen, 19) < 0) return -1;

    total = hlit + hdist;
    for (i = 0; i < total; ) {
        unsigned sym;
        if (huff_decode(br, &ch, &sym)) return -1;
        if (sym <= 15) {
            lens[i++] = (unsigned char)sym;
        } else if (sym == 16) {
            unsigned extra, rep;
            unsigned char prev;
            if (i == 0 || getbits(br, 2, &extra)) return -1;
            rep = 3 + extra;
            if (i + rep > total) return -1;
            prev = lens[i - 1];
            while (rep--) lens[i++] = prev;
        } else if (sym == 17) {
            unsigned extra, rep;
            if (getbits(br, 3, &extra)) return -1;
            rep = 3 + extra;
            if (i + rep > total) return -1;
            while (rep--) lens[i++] = 0;
        } else if (sym == 18) {
            unsigned extra, rep;
            if (getbits(br, 7, &extra)) return -1;
            rep = 11 + extra;
            if (i + rep > total) return -1;
            while (rep--) lens[i++] = 0;
        } else {
            return -1;
        }
    }

    if (lens[256] == 0) return -1;
    if (huff_build(litlen, lens, hlit) < 0) return -1;
    if (huff_build(dist, lens + hlit, hdist) < 0) return -1;
    return 0;
}

static int inflate_codes(struct bit_reader *br,
                         const struct huffman *litlen,
                         const struct huffman *dist,
                         unsigned char *out, size_t outcap, size_t *outpos)
{
    static const unsigned short lbase[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
        67,83,99,115,131,163,195,227,258
    };
    static const unsigned char lext[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,
        4,4,4,4,5,5,5,5,0
    };
    static const unsigned short dbase[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const unsigned char dext[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };

    for (;;) {
        unsigned sym;
        if (huff_decode(br, litlen, &sym)) return -1;
        if (sym < 256) {
            if (*outpos >= outcap) return -2;
            out[(*outpos)++] = (unsigned char)sym;
        } else if (sym == 256) {
            return 0;
        } else if (sym >= 257 && sym <= 285) {
            unsigned li = sym - 257, extra = 0, length;
            unsigned dsym, dextra = 0, distance;
            size_t srcpos;
            if (lext[li] && getbits(br, lext[li], &extra)) return -1;
            length = lbase[li] + extra;

            if (huff_decode(br, dist, &dsym) || dsym >= 30) return -1;
            if (dext[dsym] && getbits(br, dext[dsym], &dextra)) return -1;
            distance = dbase[dsym] + dextra;
            if (distance == 0 || distance > *outpos) return -1;
            if ((size_t)length > outcap - *outpos) return -2;

            srcpos = *outpos - distance;
            while (length--) out[(*outpos)++] = out[srcpos++];
        } else {
            return -1;
        }
    }
}

static int inflate_raw(const unsigned char *src, size_t srclen,
                       unsigned char *out, size_t outcap, size_t *written)
{
    struct bit_reader br;
    size_t opos = 0;
    unsigned final = 0;

    memset(&br, 0, sizeof br);
    br.src = src;
    br.len = srclen;

    while (!final) {
        unsigned type;
        if (getbits(&br, 1, &final) || getbits(&br, 2, &type)) return -1;

        if (type == 0) {
            unsigned len, nlen;
            byte_align(&br);
            if (br.pos + 4 > br.len) return -1;
            len = (unsigned)br.src[br.pos] | ((unsigned)br.src[br.pos + 1] << 8);
            nlen = (unsigned)br.src[br.pos + 2] | ((unsigned)br.src[br.pos + 3] << 8);
            br.pos += 4;
            if ((len ^ 0xffffu) != nlen) return -1;
            if (br.pos + len > br.len || len > outcap - opos) return -1;
            memcpy(out + opos, br.src + br.pos, len);
            br.pos += len;
            opos += len;
        } else if (type == 1 || type == 2) {
            struct huffman litlen, dist;
            int rc;
            if (type == 1) fixed_tables(&litlen, &dist);
            else if (dynamic_tables(&br, &litlen, &dist)) return -1;
            rc = inflate_codes(&br, &litlen, &dist, out, outcap, &opos);
            if (rc) return rc;
        } else {
            return -1;
        }
    }

    *written = opos;
    return 0;
}

static uint32_t crc32_bytes(const unsigned char *p, size_t n)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned k;
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int skip_zstr(const unsigned char *buf, size_t limit, size_t *pos)
{
    while (*pos < limit) {
        if (buf[(*pos)++] == 0) return 0;
    }
    return -1;
}

FILE *gu_gzip_decode_member(FILE *src, uint64_t off, uint64_t len,
                            char *errbuf, size_t errbuf_sz)
{
    unsigned char *in = NULL, *outbuf = NULL;
    size_t n, pos, payload_end, written = 0;
    uint32_t expected_crc, expected_size;
    unsigned flags;
    FILE *out = NULL;
    int rc;

    if (!src || len < 18 || len > SIZE_MAX || off > LONG_MAX ||
        fseek(src, (long)off, SEEK_SET) != 0) {
        seterr(errbuf, errbuf_sz, "invalid gzip member range");
        return NULL;
    }

    n = (size_t)len;
    in = (unsigned char *)malloc(n);
    if (!in) {
        seterr(errbuf, errbuf_sz, "cannot allocate gzip input buffer");
        return NULL;
    }
    if (fread(in, 1, n, src) != n) {
        free(in);
        seterr(errbuf, errbuf_sz, "short read in gzip member");
        return NULL;
    }

    if (in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) {
        free(in);
        seterr(errbuf, errbuf_sz, "not a gzip/deflate stream");
        return NULL;
    }
    flags = in[3];
    if (flags & GZ_RESERVED) {
        free(in);
        seterr(errbuf, errbuf_sz, "unsupported gzip header flags");
        return NULL;
    }

    pos = 10;
    payload_end = n - 8;
    if (flags & GZ_FEXTRA) {
        unsigned xlen;
        if (pos + 2 > payload_end) goto malformed;
        xlen = (unsigned)in[pos] | ((unsigned)in[pos + 1] << 8);
        pos += 2;
        if (pos + xlen > payload_end) goto malformed;
        pos += xlen;
    }
    if ((flags & GZ_FNAME) && skip_zstr(in, payload_end, &pos)) goto malformed;
    if ((flags & GZ_FCOMMENT) && skip_zstr(in, payload_end, &pos)) goto malformed;
    if (flags & GZ_FHCRC) {
        if (pos + 2 > payload_end) goto malformed;
        pos += 2; /* header CRC is optional metadata; payload CRC is verified below */
    }
    (void)GZ_FTEXT;
    if (pos > payload_end) goto malformed;

    expected_crc = le32(in + n - 8);
    expected_size = le32(in + n - 4);
    if (expected_size == 0) {
        free(in);
        seterr(errbuf, errbuf_sz, "zero/4GiB gzip ISIZE is unsupported");
        return NULL;
    }

    outbuf = (unsigned char *)malloc((size_t)expected_size);
    if (!outbuf) {
        free(in);
        seterr(errbuf, errbuf_sz, "cannot allocate gzip output buffer");
        return NULL;
    }

    rc = inflate_raw(in + pos, payload_end - pos,
                     outbuf, (size_t)expected_size, &written);
    free(in);
    if (rc != 0 || written != (size_t)expected_size) {
        free(outbuf);
        seterr(errbuf, errbuf_sz, rc == -2 ? "gzip output exceeds ISIZE" : "corrupt deflate payload");
        return NULL;
    }
    if (crc32_bytes(outbuf, written) != expected_crc) {
        free(outbuf);
        seterr(errbuf, errbuf_sz, "gzip CRC32 mismatch");
        return NULL;
    }

    out = tmpfile();
    if (!out) {
        free(outbuf);
        seterr(errbuf, errbuf_sz, "cannot create temporary gzip output stream");
        return NULL;
    }
    if (written && fwrite(outbuf, 1, written, out) != written) {
        free(outbuf);
        fclose(out);
        seterr(errbuf, errbuf_sz, "cannot write temporary gzip output stream");
        return NULL;
    }
    free(outbuf);
    if (fflush(out) != 0 || fseek(out, 0, SEEK_SET) != 0) {
        fclose(out);
        seterr(errbuf, errbuf_sz, "cannot rewind temporary gzip output stream");
        return NULL;
    }
    return out;

malformed:
    free(in);
    seterr(errbuf, errbuf_sz, "malformed gzip header");
    return NULL;
}
