// src/blkdev.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>

#include "debug.h"
#include "genhd.h"
#include "vblk.h"
#include "diskio.h"

#define LSEC 512u
#define MAX_PARTS 128
#define ENTRIES_MAX_BYTES (8u*1024u*1024u) /* 8 MiB cap */

/* ---------- Forward prototypes (avoid implicit-decl ABI bugs) ---------- */
static inline int read_lba512(vblk_t *dev, uint64_t lba, void *buf, uint32_t cnt);
static inline int read_bytes(vblk_t *dev, uint64_t off, uint32_t len, void *dst);

static int  scan_ebr_chain(vblk_t *dev, uint32_t ext_base_lba,
                           uint64_t *first, uint64_t *last, int n, int max);
static int  scan_mbr(vblk_t *dev, uint64_t *first, uint64_t *last, int max);

#pragma pack(push,1)
typedef struct {
    char     sig[8]; // "EFI PART"
    uint32_t rev, header_size, header_crc, rsv;
    uint64_t current_lba, backup_lba, first_usable_lba, last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t entries_lba;
    uint32_t num_entries, entry_size, entries_crc;
} gpt_hdr_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t first_lba, last_lba;
    uint64_t attrs;
    uint16_t name_utf16[36];
} gpt_ent_t;
#pragma pack(pop)

static int  gpt_validate_at(vblk_t *dev, uint64_t total_lbas,
                            uint64_t hdr_lba, gpt_hdr_t *out_hdr);
static int  scan_gpt(vblk_t *dev, uint64_t total_lbas,
                     uint64_t *first, uint64_t *last, int max);
static void sort_pairs(uint64_t *first, uint64_t *last, int n);
static int  register_child(const vblk_t *parent, const char *parent_name,
                           int nth, uint64_t first_lba, uint64_t last_lba,
                           const char *ptable_kind);

/* ================================ CRC32 (GPT) ================================ */
static uint32_t crc32_table[256];
static int crc_ready = 0;
static void crc_init(void){
    if (crc_ready) return;
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for (int j=0;j<8;j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i]=c;
    }
    crc_ready=1;
}
static uint32_t crc32_calc(const void *data, size_t len){
    crc_init();
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t*)data;
    for (size_t i=0;i<len;i++) c = crc32_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ================================ I/O helpers ================================ */
static inline int read_lba512(vblk_t *dev, uint64_t lba, void *buf, uint32_t cnt){
    return vblk_read_blocks(dev, (uint32_t)lba, cnt, buf) ? 0 : -1;
}
static inline int read_bytes(vblk_t *dev, uint64_t off, uint32_t len, void *dst){
    return vblk_read_bytes(dev, off, len, dst) ? 0 : -1;
}

/* ================================ MBR / EBR ================================= */
static int scan_ebr_chain(vblk_t *dev, uint32_t ext_base_lba,
                          uint64_t *first, uint64_t *last, int n, int max)
{
    uint32_t ebr_lba = ext_base_lba;
    while (n < max) {
        uint8_t sec[512];
        if (read_lba512(dev, ebr_lba, sec, 1)) {
            DBG("  EBR read failed @ LBA=%" PRIu32 " -> stop", ebr_lba);
            break;
        }
        if (sec[510]!=0x55 || sec[511]!=0xAA) {
            DBG("  EBR bad 0x55AA @ LBA=%" PRIu32 " -> stop", ebr_lba);
            break;
        }

        const uint8_t *e1 = sec + 446;        // logical partition
        const uint8_t *e2 = sec + 446 + 16;   // link to next EBR

        uint8_t  t1 = e1[4];
        uint32_t l1 = (uint32_t)e1[8]  | ((uint32_t)e1[9]  << 8) |
                      ((uint32_t)e1[10] <<16) | ((uint32_t)e1[11] <<24);
        uint32_t c1 = (uint32_t)e1[12] | ((uint32_t)e1[13] << 8) |
                      ((uint32_t)e1[14] <<16) | ((uint32_t)e1[15] <<24);

        if (t1 && c1) {
            first[n] = (uint64_t)ebr_lba + l1;
            last[n]  = first[n] + c1 - 1;
            DBG("  EBR logical #%d: first=%" PRIu64 " last=%" PRIu64, n+1, first[n], last[n]);
            n++;
        }

        uint8_t  t2 = e2[4];
        uint32_t l2 = (uint32_t)e2[8]  | ((uint32_t)e2[9]  << 8) |
                      ((uint32_t)e2[10] <<16) | ((uint32_t)e2[11] <<24);

        if (t2==0x05 || t2==0x0F || t2==0x85) {
            ebr_lba = ext_base_lba + l2;
            DBG("  EBR next link -> LBA=%" PRIu32, ebr_lba);
        } else {
            DBG("  EBR chain end");
            break;
        }
    }
    return n;
}

// returns: count>0 (MBR parts), 0 (no/invalid MBR), -1 (protective MBR -> GPT)
static int scan_mbr(vblk_t *dev, uint64_t *first, uint64_t *last, int max){
    uint8_t mbr[512];
    if (read_lba512(dev, 0, mbr, 1)) { DBG("  MBR read @0 failed"); return 0; }
    if (mbr[510]!=0x55 || mbr[511]!=0xAA) { DBG("  MBR missing 0x55AA"); return 0; }

    int n = 0;
    for (int i=0;i<4;i++) {
        const uint8_t *e = mbr + 446 + i*16;
        uint8_t  type  = e[4];
        uint32_t lba   = (uint32_t)e[8]  | ((uint32_t)e[9]  << 8) |
                         ((uint32_t)e[10] <<16) | ((uint32_t)e[11] <<24);
        uint32_t count = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                         ((uint32_t)e[14] <<16) | ((uint32_t)e[15] <<24);

        if (!type || !count) continue;
        if (type == 0xEE) { DBG("  Protective MBR found"); return -1; }

        if (type == 0x05 || type == 0x0F || type == 0x85) {
            DBG("  Extended partition @ LBA=%" PRIu32, lba);
            n = scan_ebr_chain(dev, lba, first, last, n, max);
            continue;
        }
        if (n < max) {
            first[n] = lba;
            last[n]  = lba + count - 1;
            DBG("  MBR primary #%d: first=%" PRIu64 " last=%" PRIu64, n+1, first[n], last[n]);
            n++;
        }
    }
    return n;
}

/* ================================== GPT ===================================== */
static int gpt_validate_at(vblk_t *dev, uint64_t total_lbas, uint64_t hdr_lba, gpt_hdr_t *out_hdr){
    DBG("  gpt_validate_at LBA=%" PRIu64, hdr_lba);

    uint8_t sec[512];
    if (read_lba512(dev, hdr_lba, sec, 1)) { DBG("  read header fail -> return 0"); return 0; }

    if (memcmp(sec, "EFI PART", 8) != 0) { DBG("  bad sig -> return 0"); return 0; }

    gpt_hdr_t h;
    memcpy(&h, sec, sizeof h);  // copy only our struct size

    if (h.header_size < 92 || h.entry_size < sizeof(gpt_ent_t) ||
        h.num_entries == 0 || h.num_entries > 4096) {
        DBG("  size fields invalid -> return 0");
        return 0;
    }

    if (total_lbas) {
        if (h.current_lba >= total_lbas || h.backup_lba >= total_lbas) { DBG("  hdr outside disk -> return 0"); return 0; }
        if (h.last_usable_lba >= total_lbas || h.entries_lba >= total_lbas) { DBG("  fields past end -> return 0"); return 0; }
    }

    /* header CRC over header_size bytes with the CRC field zeroed */
    uint8_t *hdrbuf = (uint8_t*)malloc(h.header_size);
    if (!hdrbuf) { DBG("  malloc hdr fail -> return 0"); return 0; }
    if (h.header_size <= sizeof sec) {
        memcpy(hdrbuf, sec, h.header_size);
    } else {
        if (read_bytes(dev, hdr_lba * (uint64_t)LSEC, h.header_size, hdrbuf)) {
            free(hdrbuf); DBG("  read hdr bytes fail -> return 0"); return 0;
        }
    }
    if (h.header_size >= 20) memset(hdrbuf + 16, 0, 4); /* zero header_crc field */
    uint32_t calc = crc32_calc(hdrbuf, h.header_size);
    free(hdrbuf);
    if (calc != h.header_crc) { DBG("  hdr CRC mismatch -> return 0"); return 0; }

    /* entries CRC */
    size_t bytes = (size_t)h.num_entries * h.entry_size;
    if (bytes == 0 || bytes > ENTRIES_MAX_BYTES) { DBG("  entries blob too large: %zu -> return 0", bytes); return 0; }
    if (total_lbas && (h.entries_lba * (uint64_t)LSEC + bytes) > total_lbas * (uint64_t)LSEC) {
        DBG("  entries table runs past end of disk -> return 0");
        return 0;
    }
    uint8_t *buf = (uint8_t*)malloc(bytes);
    if (!buf) { DBG("  malloc entries fail -> return 0"); return 0; }
    if (read_bytes(dev, h.entries_lba * (uint64_t)LSEC, (uint32_t)bytes, buf)) { free(buf); DBG("  read entries fail -> return 0"); return 0; }
    uint32_t ecrc = crc32_calc(buf, bytes);
    if (ecrc != h.entries_crc) { free(buf); DBG("  entries CRC mismatch -> return 0"); return 0; }

    free(buf);
    *out_hdr = h;
    DBG("  gpt_validate_at OK");
    return 1;
}

static int scan_gpt(vblk_t *dev, uint64_t total_lbas, uint64_t *first, uint64_t *last, int max){
    DBG("  scan_gpt: total_lbas=%" PRIu64, total_lbas);
    gpt_hdr_t h;

    if (!gpt_validate_at(dev, total_lbas, 1, &h)) {
        DBG("  primary invalid, try backup");
        uint8_t sec1[512];
        if (read_lba512(dev, 1, sec1, 1)) { DBG("  read LBA1 failed -> scan_gpt returning 0"); return 0; }
        if (memcmp(sec1, "EFI PART", 8) != 0) { DBG("  no backup hint -> scan_gpt returning 0"); return 0; }
        uint64_t backup_lba;
        memcpy(&backup_lba, sec1 + 32, sizeof backup_lba); /* backup_lba at offset 32 */
        if (!gpt_validate_at(dev, total_lbas, backup_lba, &h)) { DBG("  backup invalid -> scan_gpt returning 0"); return 0; }
        DBG("  using backup GPT header @ LBA=%" PRIu64, h.current_lba);
    } else {
        DBG("  using primary GPT header");
    }

    size_t bytes = (size_t)h.num_entries * h.entry_size;
    if (bytes == 0 || bytes > ENTRIES_MAX_BYTES) { DBG("  entries too large on second read -> scan_gpt returning 0"); return 0; }

    uint8_t *buf = (uint8_t*)malloc(bytes);
    if (!buf) { DBG("  malloc entries 2 fail -> scan_gpt returning 0"); return 0; }
    if (read_bytes(dev, h.entries_lba * (uint64_t)LSEC, (uint32_t)bytes, buf)) { free(buf); DBG("  read entries 2 fail -> scan_gpt returning 0"); return 0; }

    int n = 0;
    for (uint32_t i=0; i<h.num_entries && n<max; i++) {
        const gpt_ent_t *e = (const gpt_ent_t*)(buf + (size_t)i*h.entry_size);
        int empty = 1; for (int k=0;k<16;k++) if (e->type_guid[k]) { empty=0; break; }
        if (empty) continue;
        if (e->last_lba < e->first_lba) continue;
        if (total_lbas && (e->last_lba >= total_lbas)) continue;
        first[n] = e->first_lba;
        last[n]  = e->last_lba;
        DBG("  GPT part #%d: first=%" PRIu64 " last=%" PRIu64, n+1, first[n], last[n]);
        n++;
    }
    free(buf);
    DBG("  scan_gpt: found %d entries", n);
    return n;
}

/* =========================== Child registration ============================= */
static void sort_pairs(uint64_t *first, uint64_t *last, int n){
    for (int i=0;i<n;i++)
        for (int j=i+1;j<n;j++)
            if (first[j] < first[i]) {
                uint64_t tf=first[i], tl=last[i];
                first[i]=first[j]; last[i]=last[j];
                first[j]=tf;       last[j]=tl;
            }
}

static int register_child(const vblk_t *parent, const char *parent_name,
                          int nth, uint64_t first_lba, uint64_t last_lba,
                          const char *ptable_kind) {
    if (last_lba < first_lba) return -1;

    vblk_t child = (vblk_t){0};
    snprintf(child.name, sizeof child.name, "%s%d", parent_name, nth);
    snprintf(child.dev,  sizeof child.dev,  "%.*s", (int)sizeof(child.dev) - 1, parent->dev);
    child.part_index = nth;
    snprintf(child.fstype, sizeof child.fstype, "%s", ptable_kind ? ptable_kind : "-");
    child.lba_start = first_lba;
    child.lba_size  = last_lba - first_lba + 1;

    int idx = vblk_register(&child);
    if (idx < 0) { fprintf(stderr, "partscan: registry full when adding %s\n", child.name); return -1; }

	DBG("%-8s start=%" PRIu64 " end=%" PRIu64 " size=%.2fMB",
		child.name,
		first_lba,
		last_lba,
		(double)child.lba_size * (double)LSEC / (1024.0 * 1024.0));
		
    return 0;
}

/* ================================ Public API ================================= */
int disk_scan_partitions(struct gendisk *gd) {
    if (!gd) { DBG("disk_scan_partitions: gd==NULL -> return -1"); return -1; }

    DBG("disk_scan_partitions('%s')", gd->name);
    const vblk_t *pconst = vblk_by_name(gd->name);
    if (!pconst) { fprintf(stderr, "partscan: parent '%s' not found\n", gd->name); DBG("disk_scan_partitions: return -1 (parent not found)"); return -1; }
    vblk_t *parent = (vblk_t*)pconst;

    const char *key = parent->dev[0] ? parent->dev : parent->name;
    uint64_t size_bytes = diskio_size_bytes(key);
    uint64_t total_lbas = size_bytes ? (size_bytes / (uint64_t)LSEC) : 0;
    DBG("  devkey=%s size_bytes=%" PRIu64 " total_lbas=%" PRIu64, key, size_bytes, total_lbas);

    uint64_t first[MAX_PARTS]={0}, last[MAX_PARTS]={0};

    DBG("  try MBR...");
    int n = scan_mbr(parent, first, last, MAX_PARTS);
    DBG("  MBR result=%d", n);
    if (n > 0) {
        sort_pairs(first, last, n);
        int made=0; for (int i=0;i<n;i++) if (register_child(pconst, gd->name, i+1, first[i], last[i], "mbr")==0) made++;
        printf("partscan: registered %d MBR partition(s) on %s\n", made, gd->name);
        DBG("disk_scan_partitions: returning 0 (MBR)");
        return 0;
    }
    if (n == -1) {
        DBG("  protective MBR -> GPT");
        int g = scan_gpt(parent, total_lbas, first, last, MAX_PARTS);
        DBG("  GPT result=%d (via protective MBR)", g);
        if (g <= 0) {
            printf("partscan: protective MBR but GPT unreadable on %s\n", gd->name);
            DBG("disk_scan_partitions: returning -1 (protective MBR but GPT unreadable)");
            return -1;
        }
        sort_pairs(first, last, g);
        int made=0; for (int i=0;i<g;i++) if (register_child(pconst, gd->name, i+1, first[i], last[i], "gpt")==0) made++;
        DBG("partscan: registered %d GPT partition(s) on %s\n", made, gd->name);
        DBG("disk_scan_partitions: returning 0 (GPT via protective MBR)");
        return 0;
    }

    DBG("  try raw GPT...");
    int g = scan_gpt(parent, total_lbas, first, last, MAX_PARTS);
    DBG("  GPT result=%d", g);
    if (g > 0) {
        sort_pairs(first, last, g);
        int made=0; for (int i=0;i<g;i++) if (register_child(pconst, gd->name, i+1, first[i], last[i], "gpt")==0) made++;
        printf("partscan: registered %d GPT partition(s) on %s\n", made, gd->name);
        DBG("disk_scan_partitions: returning 0 (GPT)");
        return 0;
    }

    DBG("partscan: no partitions registered on %s", gd->name);
    DBG("disk_scan_partitions: returning 0 (no PT)");
    return 0;
}

int add_disk(struct gendisk *gd) {
    if (!gd) { DBG("add_disk: gd==NULL -> return -1"); return -1; }
    if (!vblk_by_name(gd->name)) {
        vblk_t parent = (vblk_t){0};
        snprintf(parent.name, sizeof parent.name, "%s", gd->name);
        snprintf(parent.dev,  sizeof parent.dev,  "%s", gd->name);
        parent.part_index = -1;
        snprintf(parent.fstype, sizeof parent.fstype, "-");
        parent.lba_start = 0;
        parent.lba_size  = 0;
        if (vblk_register(&parent) < 0) {
            fprintf(stderr, "add_disk: failed to register %s\n", gd->name);
            DBG("add_disk: failed to register -> return -1");
            return -1;
        }
    }
    DBG("add_disk: calling scan");
    int rc = disk_scan_partitions(gd);
    DBG("add_disk: returning rc=%d", rc);
    return rc;
}

int del_disk(const char *name) {
    (void)name;
    DBG("del_disk('%s') (stub) -> return 0", name ? name : "(null)");
    return 0;
}

int block_rescan(const char *devname) {
    if (!devname) { DBG("block_rescan: devname==NULL -> return -1"); return -1; }
    struct gendisk gd = (gendisk){0};
    snprintf(gd.name, sizeof gd.name, "%s", devname);
    DBG("block_rescan('%s') -> call disk_scan_partitions", gd.name);
    int rc = disk_scan_partitions(&gd);
    DBG("block_rescan: returning rc=%d", rc);
    return rc;
}
