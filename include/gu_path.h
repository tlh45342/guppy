#pragma once
#include <stdio.h>

typedef enum gu_path_space {
    GU_PATH_VFS = 0,
    GU_PATH_LOCAL = 1
} gu_path_space_t;

typedef struct gu_path {
    gu_path_space_t space;
    const char *path;
} gu_path_t;

/* Unqualified paths are VFS. "local:" selects the host filesystem. */
int gu_path_parse(const char *text, gu_path_t *out);

/*
 * Open a path as a seekable host FILE stream.
 *
 * LOCAL paths are fopen()'d directly.
 * VFS paths are copied into tmpfile() so existing archive parsers can remain
 * seek-oriented.  This is intentionally a Phase-1 bridge; callers do not need
 * to know which namespace supplied the bytes.
 */
FILE *gu_path_open_seekable_read(const char *text, gu_path_t *parsed_out);
