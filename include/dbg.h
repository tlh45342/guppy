#pragma once
#include <stdio.h>

extern int g_debug_iso;
extern int g_debug_vfs;

#define ISO_DBG(fmt, ...) do { if (g_debug_iso) fprintf(stderr, "[iso] " fmt "\n", ##__VA_ARGS__); } while (0)
#define VFS_DBG(fmt, ...) do { if (g_debug_vfs) fprintf(stderr, "[vfs] " fmt "\n", ##__VA_ARGS__); } 