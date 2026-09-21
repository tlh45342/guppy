#pragma once
#include <stdio.h>
#include <stdint.h>

/*
 * Decode one gzip member stored at [off, off+len) in src into a seekable
 * temporary FILE stream positioned at byte zero.
 *
 * This is intentionally small and self-contained so Guppy does not acquire
 * a host zlib dependency merely to read Debian Packages.gz indexes.
 */
FILE *gu_gzip_decode_member(FILE *src, uint64_t off, uint64_t len,
                            char *errbuf, size_t errbuf_sz);
