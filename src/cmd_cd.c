#include "cmds.h"
#include "cwd.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CD_BUF 256

/* Join base + rel and normalize:
   - If rel is absolute, ignore base
   - Collapse //, /./, and /../ (never goes above '/')
   - Remove trailing '/' except root
   Returns 1 on success, 0 on overflow/invalid. */
static int join_normalize(const char *base, const char *rel, char out[], size_t outsz) {
    char combined[CD_BUF * 2];
    if (!rel || !*rel) rel = "/";
    if (rel[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", rel);
    } else {
        if (!base || !*base) base = "/";
        if (strcmp(base, "/") == 0)
            snprintf(combined, sizeof(combined), "/%s", rel);
        else
            snprintf(combined, sizeof(combined), "%s/%s", base, rel);
    }

    // Build normalized path into out
    size_t oi = 0;
    out[oi++] = '/';
    out[oi] = '\0';

    size_t i = 0, n = strlen(combined);
    while (i < n) {
        // skip '/'
        while (i < n && combined[i] == '/') i++;
        if (i >= n) break;

        // segment start
        size_t start = i;
        while (i < n && combined[i] != '/') i++;
        size_t seglen = i - start;

        if (seglen == 1 && combined[start] == '.') {
            // skip "."
            continue;
        }
        if (seglen == 2 && combined[start] == '.' && combined[start + 1] == '.') {
            // pop last segment (not going above root)
            if (oi > 1) {
                // remove trailing '/'
                if (out[oi - 1] == '/') oi--;
                // back to previous '/'
                while (oi > 1 && out[oi - 1] != '/') oi--;
                out[oi] = '\0';
            }
            continue;
        }

        // append '/' if not root-only
        if (oi > 1) {
            if (oi + 1 >= outsz) return 0;
            out[oi++] = '/';
        }
        // append segment
        if (oi + seglen >= outsz) return 0;
        memcpy(out + oi, combined + start, seglen);
        oi += seglen;
        out[oi] = '\0';
    }

    // Remove trailing '/' except root
    if (oi > 1 && out[oi - 1] == '/') {
        out[--oi] = '\0';
    }
    return 1;
}

int cmd_cd(int argc, char **argv) {
    static char prev[CD_BUF] = "/";  // previous directory (for "cd -")
    const char *arg = (argc >= 2) ? argv[1] : "/";

    char current[CD_BUF];
    snprintf(current, sizeof(current), "%s", cwd_get() ? cwd_get() : "/");

    char target[CD_BUF];

    if (strcmp(arg, "-") == 0) {
        // swap to previous
        char tmp[CD_BUF];
        snprintf(tmp, sizeof(tmp), "%s", prev);
        snprintf(prev, sizeof(prev), "%s", current);
        snprintf(target, sizeof(target), "%s", tmp);
    } else {
        // normal path resolution
        if (!join_normalize(current, arg, target, sizeof(target))) {
            fprintf(stderr, "cd: path too long or invalid\n");
            return 1;
        }
        // update prev
        snprintf(prev, sizeof(prev), "%s", current);
    }

    cwd_set(target);
    printf("%s\n", cwd_get());
    return 0;
}
