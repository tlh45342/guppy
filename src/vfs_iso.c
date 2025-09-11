// src/vfs_iso.c (or keep your original filename)
#include "vfs.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>    // printf
// #include "uart.h"  // removed

/* Your drivers */
#include "iso9660.h"   /* bool iso_mount(vblk_t*, iso9660_t*), bool iso_read_file_by_path(...) */
#include "ext2.h"      /* bool ext2_create_and_write(const char*, const uint8_t*, uint32_t) */
#include "vblk.h"      /* your multi-device virtio-blk handle (vblk_t) */

/* ---- optional logging knob ---- */
#ifndef VFS_DEBUG
#define VFS_DEBUG 1
#endif

/* ---- tiny helpers (no libc string deps) ---- */
static int str_eq(const char *a, const char *b){
    while (*a && *b){ if (*a!=*b) return 0; a++; b++; } return (*a==0 && *b==0);
}
static int strncmp_eq(const char *a, const char *b, uint32_t n){
    while (n-- && *a && *b){ if (*a!=*b) return 0; a++; b++; } return (n==(uint32_t)~0u) || (*a==0 && *b==0);
}
static uint32_t cstrlen(const char *s){ uint32_t n=0; while (s && *s++) n++; return n; }

/* ---- VFS single-mount table ---- */
typedef struct iso9660 iso9660_t;
typedef struct vblk vblk_t;

static iso9660_t g_iso;
static vblk_t   *g_iso_dev = 0;
static char      g_iso_mpt[8] = ""; /* "/mnt" or "" */
static uint8_t   g_have_mpt = 0;
static uint8_t   g_iso_mounted = 0;

const char* vfs_iso_mountpoint(void){ return g_iso_mpt; }

int vfs_mkdir(const char *path){
    if (!path || path[0] != '/') return 0;
    /* For our use-case mkdir only establishes a mountpoint node (in-memory). */
    /* Accept only "/mnt" (you can relax later). */
    if (!str_eq(path, "/mnt")) return 0;
    /* Remember the mountpoint string */
    g_iso_mpt[0] = '/'; g_iso_mpt[1] = 'm'; g_iso_mpt[2] = 'n'; g_iso_mpt[3] = 't'; g_iso_mpt[4] = 0;
    g_have_mpt = 1;
    return 1;
}

bool vfs_mount_iso_at(const char *path, vblk_t *dev){
    if (!g_have_mpt || !str_eq(path, g_iso_mpt)) return false;
    if (!dev) return false;
    /* mount ISO9660 read-only */
    if (!iso_mount(dev, &g_iso)) return false;
    g_iso_dev = dev;
    g_iso_mounted = 1;
#if VFS_DEBUG
    printf("mounted ISO at %s\n", g_iso_mpt);
#endif
    return true;
}

bool vfs_is_iso_path(const char *path){
    if (!g_iso_mounted || !path) return false;
    uint32_t m = cstrlen(g_iso_mpt);
    if (m == 0) return false;
    /* path must be "/mnt" or "/mnt/..." */
    if (strncmp_eq(path, g_iso_mpt, m)) {
        if (path[m] == 0 || path[m] == '/') return true;
    }
    return false;
}

static const char* iso_strip_mpt(const char *path){
    /* input is guaranteed to start with mpt; return subpath beginning with '/' to be ISO-absolute */
    uint32_t m = cstrlen(g_iso_mpt);
    const char *p = path + m;
    if (*p == 0) return "/";   /* mounting point itself */
    return p;                  /* already points to "/name..." */
}

bool vfs_read_all(const char *path, uint8_t *out, uint32_t cap, uint32_t *out_len){
    if (!path || !out) return false;
    if (vfs_is_iso_path(path)) {
        const char *iso_path = iso_strip_mpt(path);   /* like "/HELLO.TXT" */
        return iso_read_file_by_path(&g_iso, iso_path, out, cap, out_len);
    }
    /* Optional: you may add ext2 reads here. For cp use-case, src is ISO. */
    return false;
}

bool vfs_write_ext2_root(const char *dst_path, const uint8_t *data, uint32_t len){
    /* Your current ext2_create_and_write() supports only "/name" at root. */
    if (!dst_path || dst_path[0] != '/' || data == 0) return false;
    /* Disallow subdirs until mkdir-on-ext2 is implemented. */
    for (const char *p = dst_path+1; *p; ++p) { if (*p == '/') return false; }
    return ext2_create_and_write(dst_path, data, len);
}
