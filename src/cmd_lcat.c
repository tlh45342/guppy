// local "cat" for host files
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "debug.h"
#ifndef DBG
#define DBG(...) do{}while(0)
#endif

static void usage(void) {
    printf("usage: lcat <file>\n");
}

int cmd_lcat(int argc, char **argv) {
    if (argc != 2) { usage(); return 1; }
    const char *path = argv[1];

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "lcat: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) {
            fprintf(stderr, "lcat: write error\n");
            fclose(f);
            return 1;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "lcat: read error\n");
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}