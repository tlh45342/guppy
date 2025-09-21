#pragma once
#include <stdint.h>
#include <stdbool.h>

// Global bitmask (defined in debug.c)
extern uint32_t g_debug_flags;

// Bits (add more as you grow)
enum {
    DBG_NONE = 0,
    DBG_ISO  = 1u << 0,   // iso9660 parser / reader
    DBG_VFS  = 1u << 1,   // virtual FS layer, mounts, paths
    DBG_ALL  = 0xFFFFFFFFu
};

// Helpers
static inline bool dbg_on(uint32_t f) { return (g_debug_flags & f) != 0; }