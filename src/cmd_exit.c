#include <stdio.h>
int cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("Type 'exit' at the REPL prompt to quit (built-in).");
    return 0;
}
