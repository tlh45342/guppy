#include "diskio.h"
#include "vdisk.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ========================= legacy host-file I/O ========================= */

bool file_pread(void *buf, size_t n, size_t off, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, (long)off, SEEK_SET) != 0) { fclose(f); return false; }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got == n;
}

bool file_pwrite(const void *buf, size_t n, size_t off, const char *path) {
    FILE *f = fopen(path, "r+b");
    if (!f) return false; /* never create/truncate an input path here */
    if (fseek(f, (long)off, SEEK_SET) != 0) { fclose(f); return false; }
    size_t put = fwrite(buf, 1, n, f);
    int ok = (put == n && fflush(f) == 0);
    fclose(f);
    return ok != 0;
}

uint64_t filesize_bytes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

/* ====================== devkey -> libvdisk registry ===================== */

#ifndef DISKIO_MAX_MAP
#define DISKIO_MAX_MAP 64
#endif
#ifndef DISKIO_PATH_MAX
#define DISKIO_PATH_MAX 512
#endif

typedef struct {
    char key[32];
    char path[DISKIO_PATH_MAX];
    vdisk_t *disk;
} diskio_map_entry_t;

static diskio_map_entry_t g_map[DISKIO_MAX_MAP];
static int g_map_count = 0;

static int map_find_index(const char *devkey) {
    for (int i=0; i<g_map_count; ++i)
        if (strcmp(g_map[i].key, devkey)==0) return i;
    return -1;
}

static int is_devkey(const char *s) {
    return s && s[0]=='/' && s[1]=='d' && s[2]=='e' && s[3]=='v' && s[4]=='/';
}

bool diskio_attach_image(const char *devkey,const char *path,uint64_t *bytes_out) {
    vdisk_t *disk = NULL;
    int idx;

    if (!devkey || !*devkey || !path || !*path) return false;
    if (vdisk_open(path, VDISK_FMT_AUTO, &disk) != 0 || !disk) return false;

    uint64_t sz = vdisk_get_size(disk);
    if (sz == 0) { vdisk_close(disk); return false; }

    idx=map_find_index(devkey);
    if (idx<0) {
        if (g_map_count>=DISKIO_MAX_MAP) { vdisk_close(disk); return false; }
        idx=g_map_count++;
        memset(&g_map[idx],0,sizeof g_map[idx]);
    } else if (g_map[idx].disk) {
        vdisk_close(g_map[idx].disk);
        g_map[idx].disk=NULL;
    }

    snprintf(g_map[idx].key,sizeof g_map[idx].key,"%.*s",
             (int)sizeof g_map[idx].key-1,devkey);
    snprintf(g_map[idx].path,sizeof g_map[idx].path,"%.*s",
             (int)sizeof g_map[idx].path-1,path);
    g_map[idx].disk=disk;
    if (bytes_out) *bytes_out=sz;
    return true;
}

bool diskio_detach(const char *devkey) {
    int idx=map_find_index(devkey);
    if(idx<0) return false;
    if(g_map[idx].disk) vdisk_close(g_map[idx].disk);
    for(int i=idx+1;i<g_map_count;++i) g_map[i-1]=g_map[i];
    --g_map_count;
    memset(&g_map[g_map_count],0,sizeof g_map[g_map_count]);
    return true;
}

const char *diskio_resolve(const char *devkey) {
    int idx=map_find_index(devkey);
    if(idx>=0) return g_map[idx].path;
    if(is_devkey(devkey)) return NULL;
    return (devkey && *devkey) ? devkey : NULL;
}

bool diskio_pread(const char *devkey,uint64_t off,void *dst,uint32_t len) {
    int idx;
    if(!dst) return false;
    idx=map_find_index(devkey);
    if(idx>=0 && g_map[idx].disk)
        return vdisk_read(g_map[idx].disk,off,dst,(size_t)len)==0;

    const char *path=diskio_resolve(devkey);
    if(!path) {
        fprintf(stderr,"diskio_pread: unmapped devkey '%s'\n",devkey?devkey:"(null)");
        return false;
    }
    return file_pread(dst,(size_t)len,(size_t)off,path);
}

bool diskio_pwrite(const char *devkey,uint64_t off,const void *src,uint32_t len) {
    int idx;
    if(!src) return false;
    idx=map_find_index(devkey);
    if(idx>=0 && g_map[idx].disk)
        return vdisk_write(g_map[idx].disk,off,src,(size_t)len)==0;

    const char *path=diskio_resolve(devkey);
    if(!path) {
        fprintf(stderr,"diskio_pwrite: unmapped devkey '%s'\n",devkey?devkey:"(null)");
        return false;
    }
    return file_pwrite(src,(size_t)len,(size_t)off,path);
}

bool diskio_flush(const char *devkey) {
    int idx=map_find_index(devkey);
    if(idx>=0 && g_map[idx].disk) return vdisk_flush(g_map[idx].disk)==0;
    return !is_devkey(devkey);
}

uint64_t diskio_size_bytes(const char *devkey) {
    int idx=map_find_index(devkey);
    if(idx>=0 && g_map[idx].disk) return vdisk_get_size(g_map[idx].disk);
    const char *path=diskio_resolve(devkey);
    return path ? filesize_bytes(path) : 0;
}
