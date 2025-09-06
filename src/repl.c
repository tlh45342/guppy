// src/repl.c

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "cmd.h"
#include "helper.h"

static int s_exit_requested = 0;
static int g_debug = 0;
static int is_true(const char *s){ return s && (*s=='1'||*s=='y'||*s=='Y'||*s=='t'||*s=='T'); }

static void rstrip_crlf(char *s){
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

void guppy_request_exit(void)      { s_exit_requested = 1; }
int  guppy_exit_requested(void)    { return s_exit_requested; }
void guppy_clear_exit_request(void){ s_exit_requested = 0; }

// split_argv(...) should already exist; if not, keep yours here
// int split_argv(char *buf, char **argv, int max);

int run_command_line(const char *line_in) {
    if (!line_in) return 0;

    // local mutable copy
    char buf[1024];
    strncpy(buf, line_in, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    rstrip_crlf(buf);

    // built-in debug toggles (not in registry)
    if (!g_debug) g_debug = is_true(getenv("GUPPY_DEBUG"));
    if (strcmp(buf, "debug on") == 0){ g_debug = 1; fprintf(stderr, "[dbg] on\n"); return 0; }
    if (strcmp(buf, "debug off")== 0){ g_debug = 0; fprintf(stderr, "[dbg] off\n"); return 0; }

    if (buf[0] == '\0' || buf[0] == '#') return 0;

    char *argv[32];
    int argc = split_argv(buf, argv, 32);
    if (argc <= 0) return 0;

    if (g_debug){
        fprintf(stderr, "[dbg] argc=%d\n", argc);
        for (int i=0;i<argc;i++) fprintf(stderr, "[dbg] argv[%d]='%s'\n", i, argv[i]);
    }

    const Command *cmd = find_command(argv[0]);
    if (!cmd) {
        fprintf(stderr, "Unknown command. Try 'help'.\n");
        return 2;
    }

    if (g_debug){
        fprintf(stderr, "[dbg] dispatch -> %s\n", cmd->name);
    }
    return cmd->fn(argc, argv);
}
