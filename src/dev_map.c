// src/dev_map.c

#include <stdio.h>
#include <string.h>
#include "devmap.h"

typedef struct { char dev[32]; char path[260]; } DeviceMap;
static DeviceMap g_dev[26];
static int g_ndev = 0;

static int idx(const char *dev) {
    for (int i=0;i<g_ndev;i++) if (strcmp(g_dev[i].dev, dev)==0) return i;
    return -1;
}

bool devmap_add(const char *dev, const char *image_path) {
    if (!dev || !image_path) return false;
    int i = idx(dev);
    if (i >= 0) {
        snprintf(g_dev[i].path, sizeof g_dev[i].path, "%s", image_path);
        return true;
    }
    if (g_ndev >= (int)(sizeof g_dev/sizeof g_dev[0])) return false;
    snprintf(g_dev[g_ndev].dev,  sizeof g_dev[0].dev,  "%s", dev);
    snprintf(g_dev[g_ndev].path, sizeof g_dev[0].path, "%s", image_path);
    g_ndev++;
    return true;
}

const char* devmap_resolve(const char *dev) {
    int i = idx(dev);
    return (i>=0) ? g_dev[i].path : NULL;
}

void devmap_list(void) {
    for (int i=0;i<g_ndev;i++) printf("%-8s  %s\n", g_dev[i].dev, g_dev[i].path);
}
