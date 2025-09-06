#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "helper.h"

// Case-insensitive compare for n characters of suffix/prefix tokens
int strncaseeq(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)tolower((unsigned char)a[i]);
        unsigned char cb = (unsigned char)tolower((unsigned char)b[i]);
        if (cb == '\0') return a[i] == '\0';
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
    }
    return 1;
}

bool starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    size_t ls = strlen(s), lp = strlen(prefix);
    return lp <= ls && strncmp(s, prefix, lp) == 0;
}

bool ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t ls = strlen(s), lt = strlen(suffix);
    if (lt > ls) return false;
    return strcmp(s + (ls - lt), suffix) == 0;
}

void rstrip(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

int is_blank_or_comment(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return (*s == '\0' || *s == ';' || *s == '#');
}

int split_argv(char *line, char **argv, int maxv) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (argc >= maxv) break;

        char *start = p;
        char quote = 0;
        if (*p == '"' || *p == '\'') { quote = *p++; start = p; }
        while (*p) {
            if (quote) {
                if (*p == quote) break;
            } else {
                if (*p == ' ' || *p == '\t') break;
            }
            p++;
        }
        if (*p) { *p = '\0'; p++; }
        argv[argc++] = start;
    }
    return argc;
}

// Accepts: plain integer bytes, or with binary suffixes: B, KiB, MiB, GiB
uint64_t parse_size(const char* s, int* ok) {
    *ok = 0;
    if (!s || !*s) return 0;

    char *end = NULL;
    double val = strtod(s, &end);
    if (end == s) return 0;

    while (*end == ' ') end++;

    uint64_t factor = 1;
    if (*end == '\0') {
        factor = 1;
    } else if (strncaseeq(end, "B", 1)) {
        factor = 1;
    } else if (strncaseeq(end, "KiB", 3)) {
        factor = 1024ULL;
    } else if (strncaseeq(end, "MiB", 3)) {
        factor = 1024ULL * 1024ULL;
    } else if (strncaseeq(end, "GiB", 3)) {
        factor = 1024ULL * 1024ULL * 1024ULL;
    } else {
        return 0;
    }

    if (val < 0) return 0;
    long double bytes_ld = (long double)val * (long double)factor;
    if (bytes_ld > (long double)UINT64_MAX) return 0;

    uint64_t bytes = (uint64_t)(bytes_ld + 0.5L);
    *ok = 1;
    return bytes;
}

double bytes_to_mib(uint64_t b) {
    return (double)b / (1024.0 * 1024.0);
}
