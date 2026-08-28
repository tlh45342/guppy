// src/helper.c

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "helper.h"
#include "devmap.h"

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

/*
 * Parse a human-readable byte size.
 *
 * Preferred short forms use binary powers, matching traditional disk-tool
 * notation:
 *     K, M, G       -> 1024, 1024^2, 1024^3
 *
 * Explicit IEC forms are equivalent:
 *     KiB, MiB, GiB -> 1024, 1024^2, 1024^3
 *
 * Explicit SI forms use decimal powers:
 *     KB, MB, GB    -> 1000, 1000^2, 1000^3
 *
 * Plain numbers (and an optional B suffix) are bytes.
 */
uint64_t parse_size(const char* s, int* ok) {
    *ok = 0;
    if (!s || !*s) return 0;

    char *end = NULL;
    long double val = strtold(s, &end);
    if (end == s || val < 0) return 0;

    while (*end == ' ' || *end == '\t') end++;

    char suffix[8];
    size_t n = 0;
    while (*end && n + 1 < sizeof suffix) {
        if (*end != ' ' && *end != '\t') suffix[n++] = *end;
        end++;
    }
    suffix[n] = '\0';

    uint64_t factor = 0;
    if (suffix[0] == '\0' || strncaseeq(suffix, "B", 2)) {
        factor = 1ULL;
    } else if (strncaseeq(suffix, "K", 2) || strncaseeq(suffix, "Ki", 3) || strncaseeq(suffix, "KiB", 4)) {
        factor = 1024ULL;
    } else if (strncaseeq(suffix, "M", 2) || strncaseeq(suffix, "Mi", 3) || strncaseeq(suffix, "MiB", 4)) {
        factor = 1024ULL * 1024ULL;
    } else if (strncaseeq(suffix, "G", 2) || strncaseeq(suffix, "Gi", 3) || strncaseeq(suffix, "GiB", 4)) {
        factor = 1024ULL * 1024ULL * 1024ULL;
    } else if (strncaseeq(suffix, "KB", 3)) {
        factor = 1000ULL;
    } else if (strncaseeq(suffix, "MB", 3)) {
        factor = 1000ULL * 1000ULL;
    } else if (strncaseeq(suffix, "GB", 3)) {
        factor = 1000ULL * 1000ULL * 1000ULL;
    } else {
        return 0;
    }

    long double bytes_ld = val * (long double)factor;
    if (bytes_ld > (long double)UINT64_MAX) return 0;

    uint64_t bytes = (uint64_t)(bytes_ld + 0.5L);
    *ok = 1;
    return bytes;
}

double bytes_to_mib(uint64_t b) {
    return (double)b / (1024.0 * 1024.0);
}

const char* resolve_image_or_dev(const char *arg) {
    if (!arg || !*arg) return NULL;
    if (strncmp(arg, "/dev/", 5) == 0) {
        // Look up mapped image for this device alias
        return devmap_resolve(arg);  // may be NULL if not mapped
    }
    // Treat as a literal path; let callers handle file existence
    return arg;
}
