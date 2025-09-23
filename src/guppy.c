// guppy.c — CLI + REPL + script runner

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "helper.h"
#include "fileutil.h"
#include "mbr.h"
#include "cmds.h"
#include "version.h"

// ---------- small helpers ----------

// ---- 'do' command implementation (script runner) ----
static int handle_do(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: do <scriptfile> [-v]\n");
        return 2;
    }
    const char *path = argv[1];
    int verbose = 0;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        else { printf("do: unknown option '%s'\n", argv[i]); return 2; }
    }

    FILE *f = fopen(path, "r");
    if (!f) { perror("do/fopen"); return 2; }

    char line[1024];
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        rstrip(line);
        if (is_blank_or_comment(line)) continue;
        if (verbose) printf(">> %s\n", line);

        int this_rc = run_command_line(line);
        if (this_rc != 0) {
            printf("(rc=%d) — stopping script at line: %s\n", this_rc, line);
            rc = this_rc;
            break;
        }
    }
    fclose(f);
    return rc;
}

// ---------- REPL ----------
#ifndef GUPPY_RC_EXIT
#define GUPPY_RC_EXIT 255  // unique sentinel; only 'exit' should return this
#endif

static int repl_loop(void) {
    guppy_clear_exit_request();               // reset when REPL starts
    setvbuf(stdout, NULL, _IONBF, 0);         // unbuffered prompts on Windows/Cygwin
    setvbuf(stderr, NULL, _IONBF, 0);

    char line[1024];
	
	#ifdef DEBUG
		printf("DEBUG macro is defined (debug mode ON)\n");
	#else
		printf("DEBUG macro is not defined (debug mode OFF)\n");
	#endif
	
	#ifdef DEBUG
		printf("Guppy %s — REPL. Type 'help' or 'exit'.\n", GUPPY_VERSION);
	#endif
			
    for (;;) {
        printf("guppy> ");
        fflush(stdout);

        if (!fgets(line, sizeof line, stdin)) {
            // EOF or error: don't silently exit—just continue or break cleanly
            if (feof(stdin)) {
                fprintf(stderr, "[repl] EOF on stdin; use 'exit' to quit next time.\n");
                break;
            }
            if (ferror(stdin)) {
                clearerr(stdin);
                fprintf(stderr, "[repl] stdin error cleared; continuing.\n");
                continue;
            }
        }

        // Run the command. We IGNORE rc unless 'exit' set the flag below.
        (void)run_command_line(line);

        if (guppy_exit_requested()) {
			#ifdef DEBUG
				fprintf(stderr, "[repl] exit requested.\n");
			#endif
            break;
        }
        // Keep looping no matter what other rc values were returned.
    }
    return 0;
}

// ---------- CLI entry ----------
int main(int argc, char **argv) {
    // No arguments → REPL
    if (argc <= 1) return repl_loop();

    // If first arg ends with .script, run it via the script runner.
    if (ends_with(argv[1], ".script")) {
        // emulate: do <file>
        char *do_argv[3];
        do_argv[0] = (char*)"do";
        do_argv[1] = argv[1];
        do_argv[2] = NULL;

        int rc = handle_do(2, do_argv);
        if (rc == 0 && argc >= 3 && strcmp(argv[2], "--interactive") == 0) {
            return repl_loop();
        }
        return rc;
    }

    // If they explicitly used "do" as the subcommand, pass through.
    if (strcmp(argv[1], "do") == 0) {
        return handle_do(argc - 1, argv + 1);
    }

    // One-shot command: join args and dispatch via registry
    {
        size_t total = 0;
        for (int i = 1; i < argc; ++i) total += strlen(argv[i]) + 1;
        char *line = (char*)malloc(total + 1);
        if (!line) return 1;
        line[0] = '\0';
        for (int i = 1; i < argc; ++i) {
            strcat(line, argv[i]);
            if (i + 1 < argc) strcat(line, " ");
        }
        int rc = run_command_line(line);
        free(line);

        // optional: drop into REPL if they passed --interactive
        if (rc == 0) {
            for (int i = 1; i < argc; ++i) {
                if (strcmp(argv[i], "--interactive") == 0) {
                    return repl_loop();
                }
            }
        }
        return rc;
    }
}
