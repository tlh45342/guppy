// src/cmd_exit.c
#include "cmd.h"

int cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    guppy_request_exit();
    return GUPPY_RC_EXIT;  // tell caller loops to unwind
}