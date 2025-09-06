// guppy.c — CLI + REPL + script runner
// Build: gcc -Wall -Wextra -O2 -std=c11 -o guppy guppy.c

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "helper.h"
#include "fileutil.h"
#include "mbr.h"

#define GUPPY_VERSION "0.0.3"
#define SECTOR_SIZE   512

// ---- forward decls used in this TU ----
static int process_cmd(int argc, char **argv);
static int execute_line(const char *line_in);   // forward so run_command_line can call it

// Public wrapper so other TUs (e.g., a separate cmd_do.c) can reuse our line executor.
int run_command_line(char *line) {
    return execute_line(line);
}

// ---------- usage ----------
static void usage(const char* argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s                # start REPL\n"
        "  %s create <img> --size <N[KiB|MiB|GiB]> [--mbr]\n"
        "  %s part add <img> --index <1..4> --type <0xNN> --start <NMiB> --size <NMiB>\n"
        "  %s mbr print <img>\n"
        "  %s do <scriptfile> [-v]\n",
        argv0, argv0, argv0, argv0, argv0);
}

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

        // Make a mutable copy since parsers may modify the buffer
        char buf[1024];
        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        int this_rc = execute_line(buf);
        if (this_rc != 0) {
            printf("(rc=%d) — stopping script at line: %s\n", this_rc, line);
            rc = this_rc;
            break;
        }
    }
    fclose(f);
    return rc;
}

// ---- command dispatcher ----
static int process_cmd(int argc, char **argv) {
    const char *cmd = argv[0];

    if (strcmp(cmd, "help") == 0) { usage("guppy"); return 0; }
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) { exit(0); }
    if (strcmp(cmd, "do") == 0) { return handle_do(argc, argv); }

    // create
    if (strcmp(cmd, "create") == 0) {
        if (argc < 3) { fprintf(stderr, "create: not enough arguments\n"); return 2; }
        const char* img = argv[1];
        uint64_t size = 0;
        bool want_mbr = false;

        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--size") == 0) {
                if (i + 1 >= argc) return 2;
                int ok = 0; size = parse_size(argv[++i], &ok);
                if (!ok || size == 0) { fprintf(stderr, "invalid --size\n"); return 2; }
            } else if (strncmp(argv[i], "--size=", 7) == 0) {
                int ok = 0; size = parse_size(argv[i] + 7, &ok);
                if (!ok || size == 0) { fprintf(stderr, "invalid --size value\n"); return 2; }
            } else if (strcmp(argv[i], "--mbr") == 0) {
                want_mbr = true;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return 2;
            }
        }
        if (file_ensure_size(img, size) != 0) { perror("create image"); return 1; }
        if (want_mbr && mbr_init_empty(img) != 0) { fprintf(stderr, "failed to write MBR\n"); return 1; }
        printf("Created %s (%llu bytes)%s\n", img, (unsigned long long)size, want_mbr ? " with MBR" : "");
        return 0;
    }

    // part add
    if (strcmp(cmd, "part") == 0 && argc >= 2 && strcmp(argv[1], "add") == 0) {
        if (argc < 3) { fprintf(stderr, "part add: not enough arguments\n"); return 2; }
        const char* img = argv[2];
        int index = -1, type = -1;
        uint64_t start = 0, sz = 0;

        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--index") == 0) {
                if (i + 1 >= argc) return 2;
                index = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--type") == 0) {
                if (i + 1 >= argc) return 2;
                const char* s = argv[++i];
                if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) type = (int)strtoul(s, NULL, 16);
                else type = atoi(s);
            } else if (strcmp(argv[i], "--start") == 0) {
                if (i + 1 >= argc) return 2;
                int ok = 0; start = parse_size(argv[++i], &ok); if (!ok) { fprintf(stderr, "invalid --start\n"); return 2; }
            } else if (strcmp(argv[i], "--size") == 0) {
                if (i + 1 >= argc) return 2;
                int ok = 0; sz = parse_size(argv[++i], &ok); if (!ok) { fprintf(stderr, "invalid --size\n"); return 2; }
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return 2;
            }
        }

        if (index < 1 || index > 4) { fprintf(stderr, "--index must be 1..4\n"); return 2; }
        if (type < 0 || type > 255) { fprintf(stderr, "--type must be 0..255\n"); return 2; }
        if (sz == 0) { fprintf(stderr, "--size must be > 0\n"); return 2; }
        if ((start % SECTOR_SIZE) != 0) { fprintf(stderr, "--start must be 512B aligned\n"); return 2; }
        if ((sz    % SECTOR_SIZE) != 0) { fprintf(stderr, "--size must be 512B aligned\n"); return 2; }

        int rc = mbr_add_partition(img, index, (uint8_t)type, start, sz);
        if (rc != 0) { fprintf(stderr, "failed to add partition (rc=%d)\n", rc); return 1; }
        printf("Added partition %d type=0x%02X start=%.1f MiB size=%.1f MiB\n",
               index, type, bytes_to_mib(start), bytes_to_mib(sz));
        return 0;
    }

    // mbr print
    if (strcmp(cmd, "mbr") == 0 && argc >= 2 && strcmp(argv[1], "print") == 0) {
        if (argc < 3) { fprintf(stderr, "mbr print: missing image\n"); return 2; }
        return mbr_print(argv[2]);
    }

    fprintf(stderr, "Unknown command. Try 'help'.\n");
    return 2;
}

// ---- central executor used by REPL and scripts ----
static int execute_line(const char *line_in) {
    char buf[1024];
    strncpy(buf, line_in, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    rstrip(buf);
    if (is_blank_or_comment(buf)) return 0;

    char *argv[32];
    int argc = split_argv(buf, argv, 32);
    if (argc == 0) return 0;
    return process_cmd(argc, argv);
}

// ---------- REPL ----------
static int repl_loop(void) {
    char line[1024];
    printf("Guppy %s — REPL. Type 'help' or 'exit'.\n", GUPPY_VERSION);
    for (;;) {
        printf("guppy> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        (void)execute_line(line);
    }
    return 0;
}

// ---------- CLI entry ----------
int main(int argc, char **argv) {
    // No arguments → REPL
    if (argc <= 1) {
        return repl_loop();
    }

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

    // 1-shot command, then optional REPL if --interactive
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
