#include <stdio.h>
#include <string.h>
#include "mnttab.h"

static MountEntry g_mnt[16];
static int g_nmnt = 0;

bool mnttab_add(const char *dev, int part_index, const char *fstype, const char *mpoint) {
    if (!dev || !mpoint) return false;
    if (g_nmnt >= (int)(sizeof g_mnt/sizeof g_mnt[0])) return false;
    snprintf(g_mnt[g_nmnt].dev, sizeof g_mnt[0].dev, "%s", dev);
    g_mnt[g_nmnt].part_index = part_index;
    snprintf(g_mnt[g_nmnt].fstype, sizeof g_mnt[0].fstype, "%s", fstype ? fstype : "");
    snprintf(g_mnt[g_nmnt].mpoint, sizeof g_mnt[0].mpoint, "%s", mpoint);
    g_nmnt++;
    return true;
}

void mnttab_list(void) { // legacy/simple
    for (int i=0;i<g_nmnt;i++) {
        const MountEntry *m = &g_mnt[i];
        printf("%-8s  %-8s  part=%d  fstype=%s\n",
               m->dev, m->mpoint, m->part_index, m->fstype[0] ? m->fstype : "-");
    }
}

const MountEntry* mnttab_find_by_mpoint(const char *mp) {
    for (int i=0;i<g_nmnt;i++) if (strcmp(g_mnt[i].mpoint, mp)==0) return &g_mnt[i];
    return NULL;
}

// NEW:
int mnttab_count(void) { return g_nmnt; }
const MountEntry* mnttab_get(int index) {
    return (index >= 0 && index < g_nmnt) ? &g_mnt[index] : NULL;
}
