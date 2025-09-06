#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// String helpers
int   strncaseeq(const char* a, const char* b, size_t n);
bool  starts_with(const char *s, const char *prefix);
bool  ends_with(const char *s, const char *suffix);

// Line helpers
void  rstrip(char *s);
int   is_blank_or_comment(const char *s); // treats leading ; or # as comment

// Arg splitting (handles quoted tokens "like this" or 'like this')
int   split_argv(char *line, char **argv, int maxv);

// Size parsing & formatting
// Accepts: bytes (plain number), B, KiB, MiB, GiB (binary powers)
uint64_t parse_size(const char *s, int *ok);
double   bytes_to_mib(uint64_t b);

bool starts_with(const char *s, const char *prefix);
void rstrip(char *s);
void lstrip(char *s);
void trim(char *s);

