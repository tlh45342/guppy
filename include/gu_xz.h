#pragma once
#include <stdio.h>
#include <stdint.h>

/*
 * Decode one .xz member stored at [off, off+len) in src into a seekable
 * temporary FILE stream positioned at byte zero.
 *
 * Returns NULL on error. If errbuf is supplied, a short diagnostic is written.
 */
FILE *gu_xz_decode_member(FILE *src, uint64_t off, uint64_t len,
                          char *errbuf, size_t errbuf_sz);
