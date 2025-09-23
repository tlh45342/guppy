// src/debug.c
#include <stdarg.h>
#include <stdio.h>
#include "debug.h"

/* Off by default; set bits at runtime (e.g., via a REPL command or test code) */
uint32_t g_debug_flags = 0;

/* You can map 'cat' to a label if you want prettier prefixes later. */
static const char *cat_label(uint32_t cat) {
    if (cat & DBG_ISO)  return "iso";
    if (cat & DBG_VFS)  return "vfs";
    if (cat & DBG_BLK)  return "blk";
    if (cat & DBG_SCAN) return "scan";
    return "dbg";
}

void debug_printf(uint32_t cat, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%s] ", cat_label(cat));
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
