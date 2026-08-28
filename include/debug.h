// include/debug.h
#pragma once
#include <stdint.h>
#include <stdio.h>

/* Debug support is always compiled in. Runtime output is controlled
   entirely by g_debug_flags, which defaults to DBG_NONE. */

/* -------- Runtime categories (bitmask) -------- */
enum {
    DBG_ISO   = 1u << 0,
    DBG_VFS   = 1u << 1,
    DBG_BLK   = 1u << 2,
    DBG_SCAN  = 1u << 3,
    DBG_MISC  = 1u << 4,
    // add more as needed
};

/* Global runtime mask (defined in src/debug.c) */
extern uint32_t g_debug_flags;

/* Default category for this translation unit.
   A .c file can override before including debug.h:
     #define DBG_CAT DBG_VFS
     #include "debug.h"
*/
#ifndef DBG_CAT
#define DBG_CAT DBG_MISC
#endif

/* Unified formatter. */
void debug_printf(uint32_t cat, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf,2,3)))
#endif
;

/* Debug calls are always available, but emit only when their runtime
   category bit is enabled in g_debug_flags. */
#define DBG(fmt, ...)  do { \
    if (g_debug_flags & (DBG_CAT)) debug_printf((DBG_CAT), fmt, ##__VA_ARGS__); \
} while (0)

#define DBGf(cat, fmt, ...)  do { \
    if (g_debug_flags & (cat)) debug_printf((cat), fmt, ##__VA_ARGS__); \
} while (0)


/* NEW: masks expected by cmd_debug.c */
#define DBG_NONE 0u
#define DBG_ALL  (DBG_ISO | DBG_VFS | DBG_BLK | DBG_SCAN | DBG_MISC)

/* NEW: helper used by cmd_debug.c’s print_status() */
#define dbg_on(mask)  ((g_debug_flags & (mask)) != 0u)
