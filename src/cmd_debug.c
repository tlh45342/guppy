#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "debug.h"
#include "cmds.h"      // Command proto typedefs
#include "debug.h"     // g_debug_flags + bits

static int ieq(const char *a, const char *b) {
    while (*a && *b) { unsigned ca=(unsigned char)tolower(*a++), cb=(unsigned char)tolower(*b++); if (ca!=cb) return 0; }
    return *a==0 && *b==0;
}

static uint32_t parse_flag(const char *s) {
    if (ieq(s, "iso"))  return DBG_ISO;
    if (ieq(s, "vfs"))  return DBG_VFS;
    if (ieq(s, "all"))  return DBG_ALL;
    if (ieq(s, "none")) return DBG_NONE;
    return 0; // unknown
}

static void print_status(void) {
    printf("debug: flags=0x%08X [", g_debug_flags);
    int first = 1;
    if (dbg_on(DBG_ISO)) { printf("%siso", first?"":"|"); first=0; }
    if (dbg_on(DBG_VFS)) { printf("%svfs", first?"":"|"); first=0; }
    if (g_debug_flags == 0) printf("none");
    printf("]\n");
}

int cmd_debug(int argc, char **argv) {
    // Forms:
    //   debug                      -> show status
    //   debug all on|off|toggle
    //   debug iso on|off|toggle
    //   debug vfs on|off|toggle
    //   debug none                 -> clear all
    if (argc == 1) { print_status(); return 0; }

    const char *which  = argv[1];
    const char *action = (argc >= 3) ? argv[2] : "toggle";

    uint32_t mask = parse_flag(which);
    if (mask == 0 && !ieq(which, "none")) {
        printf("Unknown debug flag: %s (use iso|vfs|all|none)\n", which);
        return 2;
    }

    if (ieq(which, "none")) {
        g_debug_flags = DBG_NONE;
        print_status();
        return 0;
    }

    if      (ieq(action, "on"))     g_debug_flags |=  mask;
    else if (ieq(action, "off"))    g_debug_flags &= ~mask;
    else if (ieq(action, "toggle")) g_debug_flags ^=  mask;
    else {
        printf("Unknown action: %s (use on|off|toggle)\n", action);
        return 3;
    }

    print_status();
    return 0;
}
