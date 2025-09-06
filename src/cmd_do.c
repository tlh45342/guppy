// src/do.c
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "cmd.h"

// Provided by guppy.c (wrapper around its internal execute_line)
extern int run_command_line(char *line);

// local helpers (dup’d to keep this TU self-contained)
static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' ' || s[n-1]=='\t')) s[--n] = '\0';
}

static int is_blank_or_comment(const char *s) {
    while (*s==' '||*s=='\t') s++;
    return (*s=='\0' || *s==';' || *s=='#');
}

int cmd_do(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: do <scriptfile> [-v]\n");
        return 2;
    }
    const char *path = argv[1];
    int verbose = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else {
            fprintf(stderr, "do: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        perror("do/fopen");
        return 2;
    }

    char line[1024];
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        rstrip(line);
        if (is_blank_or_comment(line)) continue;
        if (verbose) printf(">> %s\n", line);

        // Make a mutable copy because some parsers may modify the buffer
        char buf[1024];
        strncpy(buf, line, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';

        int this_rc = run_command_line(buf);
        if (this_rc != 0) {
            printf("(rc=%d) — stopping script at line: %s\n", this_rc, line);
            rc = this_rc;
            break;
        }
    }

    fclose(f);
    return rc;
}
