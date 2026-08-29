// src/cmd_sha256.c -- SHA-256 a file through the Guppy VFS

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cmds.h"
#include "vfs.h"

typedef struct {
    uint32_t h[8];
    uint64_t total;
    uint8_t  block[64];
    size_t   used;
} sha256_ctx_t;

static uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static const uint32_t k256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void sha256_transform(sha256_ctx_t *c, const uint8_t block[64]) {
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) w[i] = load_be32(block + i * 4u);
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a=c->h[0], b=c->h[1], cc=c->h[2], d=c->h[3];
    uint32_t e=c->h[4], f=c->h[5], g=c->h[6], h=c->h[7];

    for (unsigned i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k256[i] + w[i];
        uint32_t s0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }

    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d;
    c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}

static void sha256_init(sha256_ctx_t *c) {
    static const uint32_t iv[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    memcpy(c->h, iv, sizeof iv);
    c->total = 0;
    c->used = 0;
}

static void sha256_update(sha256_ctx_t *c, const void *data_, size_t len) {
    const uint8_t *data = (const uint8_t *)data_;
    c->total += (uint64_t)len;

    while (len) {
        size_t n = 64u - c->used;
        if (n > len) n = len;
        memcpy(c->block + c->used, data, n);
        c->used += n;
        data += n;
        len -= n;
        if (c->used == 64u) {
            sha256_transform(c, c->block);
            c->used = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *c, uint8_t out[32]) {
    uint64_t bits = c->total * 8u;
    c->block[c->used++] = 0x80;

    if (c->used > 56u) {
        memset(c->block + c->used, 0, 64u - c->used);
        sha256_transform(c, c->block);
        c->used = 0;
    }
    memset(c->block + c->used, 0, 56u - c->used);
    for (unsigned i = 0; i < 8; ++i)
        c->block[63u - i] = (uint8_t)(bits >> (i * 8u));
    sha256_transform(c, c->block);

    for (unsigned i = 0; i < 8; ++i) store_be32(out + i * 4u, c->h[i]);
}

static int sha256_vfs_file(const char *path) {
    struct file *f = NULL;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0 || !f) {
        fprintf(stderr, "sha256: cannot open '%s'\n", path);
        return 1;
    }

    sha256_ctx_t ctx;
    sha256_init(&ctx);

    uint8_t buf[32768];
    for (;;) {
        ssize_t n = vfs_read(f, buf, sizeof buf);
        if (n < 0) {
            fprintf(stderr, "sha256: read failed for '%s'\n", path);
            (void)vfs_close(f);
            return 1;
        }
        if (n == 0) break;
        sha256_update(&ctx, buf, (size_t)n);
    }

    if (vfs_close(f) != 0) {
        fprintf(stderr, "sha256: close failed for '%s'\n", path);
        return 1;
    }

    uint8_t digest[32];
    sha256_final(&ctx, digest);
    for (unsigned i = 0; i < sizeof digest; ++i) printf("%02x", digest[i]);
    printf("  %s\n", path);
    return 0;
}

int cmd_sha256(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: sha256 <file> [file...]\n");
        return 2;
    }

    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (sha256_vfs_file(argv[i]) != 0) rc = 1;
    }
    return rc;
}
