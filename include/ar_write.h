#pragma once
#include <stdio.h>
#include <stdint.h>
int gu_ar_write_begin(FILE *out);
int gu_ar_write_member(FILE *out, const char *name, FILE *in, uint64_t size, unsigned mode);
