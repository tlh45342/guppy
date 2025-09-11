// src/cwd.c

#include <string.h>

#include "cwd.h"

static char g_cwd[256] = "/";

const char* cwd_get(void) {
    return g_cwd;
}

void cwd_set(const char *path) {
    if (!path || !*path) { strcpy(g_cwd, "/"); return; }
    // Simple normalization: ensure it starts with '/', trim trailing '/' (except root)
    size_t n = strlen(path);
    if (path[0] != '/') {
        // Reject non-absolute for now; keep as-is or force absolute
        // For now, just copy and let future 'cd' handle relative.
        strncpy(g_cwd, path, sizeof(g_cwd)-1);
        g_cwd[sizeof(g_cwd)-1] = 0;
        return;
    }
    strncpy(g_cwd, path, sizeof(g_cwd)-1);
    g_cwd[sizeof(g_cwd)-1] = 0;
    n = strlen(g_cwd);
    if (n > 1 && g_cwd[n-1] == '/')
        g_cwd[n-1] = 0;
}