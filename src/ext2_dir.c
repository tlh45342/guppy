
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ext2.h"

/* Normalize and quick checks. Keeps behavior predictable while NYI. */
static bool is_abs_norm(const char *p) {
    return p && p[0] == '/' && p[1] != '\0';
}

/* Create a single directory. Short-term: report NYI, return false. */
bool ext2_mkdir(const char *path) {
    if (!is_abs_norm(path)) {
        fprintf(stderr, "ext2_mkdir: invalid path (need absolute, non-root): %s\n",
                path ? path : "(null)");
        return false;
    }
    fprintf(stderr, "ext2_mkdir: not yet implemented for %s\n", path);
    return false;
}

/* mkdir -p behavior: iterate components; for now we call ext2_mkdir on each.
   This will return false when the first component creation fails — which is
   the correct/explicit behavior while mkdir is not implemented yet. */
bool ext2_mkdir_p(const char *path) {
    if (!is_abs_norm(path)) {
        fprintf(stderr, "ext2_mkdir_p: invalid path (need absolute, non-root): %s\n",
                path ? path : "(null)");
        return false;
    }

    /* Walk components: /a/b/c → make /a, /a/b, /a/b/c */
    char tmp[512];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) {
        fprintf(stderr, "ext2_mkdir_p: path too long\n");
        return false;
    }
    memcpy(tmp, path, n + 1);

    char *p = tmp + 1;            // skip leading '/'
    while (*p) {
        while (*p && *p != '/') p++;
        char save = *p;
        *p = '\0';

        char partial[512];
        partial[0] = '/';
        memcpy(partial + 1, tmp + 1, (size_t)(p - (tmp + 1)));
        partial[1 + (size_t)(p - (tmp + 1))] = '\0';

        if (!ext2_mkdir(partial)) {
            /* Fails for now since ext2_mkdir is NYI; this is fine and explicit. */
            return false;
        }

        *p = save;
        if (*p == '/') p++;
    }
    return true;
}