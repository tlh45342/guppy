// src/cmd_do.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "cmds.h"      // run_command_line, GUPPY_RC_EXIT, guppy_exit_requested()
#include "helper.h"   // rstrip, is_blank_or_comment

int cmd_do(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: do <scriptfile|-> [-v]\n");
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

    FILE *f = NULL;
    int reading_stdin = (strcmp(path, "-") == 0);
    if (reading_stdin) {
        f = stdin;
    } else {
        f = fopen(path, "r");
        if (!f) { perror("do/fopen"); return 2; }
    }

    char line[1024];
    int rc = 0;
    int line_no = 0;

    while (fgets(line, sizeof line, f)) {
        ++line_no;
        rstrip(line);
        if (is_blank_or_comment(line)) continue;

        if (verbose) printf(">> %s\n", line);

        rc = run_command_line(line);

        // Respect Option C exit signaling
        if (rc == GUPPY_RC_EXIT || guppy_exit_requested()) {
            // Propagate the exit intent to caller (REPL or CLI wrapper)
            break;
        }

        if (rc != 0) {
            printf("(rc=%d) — stopping script at %s:%d: %s\n",
                   rc, reading_stdin ? "<stdin>" : path, line_no, line);
            break;
        }
    }

    if (!reading_stdin) fclose(f);
    return rc;
}
