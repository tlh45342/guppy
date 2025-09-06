#pragma once
#include <stddef.h>
#include <stdint.h>

/* Returns nonzero if s starts with prefix */
int starts_with(const char *s, const char *prefix);

/* Parse sizes like "256MiB", "32M", "4096", "1GiB". Returns bytes (>=0), or -1 on error. */
long long parse_size_bytes(const char *text);

/* Accepts "--size=256MiB" or "256MiB" */
long long parse_size_arg(const char *arg);

/* Split a line into argv; supports quotes ("...") and trims CR/LF. Returns argc. */
int parse_argv(char *line, int maxv, char **argv);
