// src/cmd_gpt.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>

#include "diskio.h"
#include "vblk.h"
#include "genhd.h"

#include <strings.h>                  // for strcasecmp on POSIX/Cygwin
#if defined(_MSC_VER) && !defined(strcasecmp)
#define strcasecmp _stricmp
#endif

/* ------------------------------- constants -------------------------------- */
#define LSEC 512u

/* We create standard 128 entries × 128 bytes (16 KiB tables) */
#define ENTRIES_MAX        128u
#define ENTRY_SIZE         128u
#define ENTRIES_BYTES      ((size_t)ENTRIES_MAX * ENTRY_SIZE)     /* 16384 */
#define ENTRIES_SECTORS    (ENTRIES_BYTES / LSEC)                 /* 32    */
#define HDR_SIZE           92u

/* ------------------------------- on-disk structs --------------------------- */
#pragma pack(push,1)
typedef struct {
    char     sig[8]; // "EFI PART"
    uint32_t rev;
    uint32_t header_size;
    uint32_t header_crc;
    uint32_t rsv;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t entries_lba;
    uint32_t num_entries;
    uint32_t entry_size;
    uint32_t entries_crc;
} gpt_hdr_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name_utf16[36];     // UTF-16LE, NUL-terminated if shorter
} gpt_ent_t;
#pragma pack(pop)

/* ------------------------------- CRC32 ------------------------------------ */
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

/* ------------------------------- helpers ---------------------------------- */
static void to_utf16le(const char *src, uint16_t dst[36]){
    size_t i=0;
    for (; src[i] && i<36; ++i) dst[i] = (uint8_t)src[i];  // ASCII→UTF16LE
    if (i<36) dst[i]=0;
}
static void from_utf16le(const uint16_t src[36], char *dst, size_t dstsz){
    size_t j=0;
    for (size_t i=0;i<36 && j+1<dstsz; ++i){
        uint16_t c = src[i];
        if (c==0) break;
        dst[j++] = (char)(c & 0xFF);
    }
    dst[j] = 0;
}
static void print_guid(const uint8_t g[16]){
    /* GUID as canonical text (mixed endianness) */
    uint32_t d1 = (uint32_t)g[3]<<24 | (uint32_t)g[2]<<16 | (uint32_t)g[1]<<8 | g[0];
    uint16_t d2 = (uint16_t)g[5]<<8 | g[4];
    uint16_t d3 = (uint16_t)g[7]<<8 | g[6];
    printf("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
           d1, d2, d3, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}
static void gen_guid(uint8_t out[16]){
    uint32_t r = (uint32_t)time(NULL) ^ (uint32_t)rand();
    for (int i=0;i<16;i++){ r ^= r<<13; r ^= r>>17; r ^= r<<5; out[i]=(uint8_t)(r & 0xFF); }
    out[7] = (out[7] & 0x0F) | 0x40; /* version 4-ish */
    out[8] = (out[8] & 0x3F) | 0x80; /* variant 10 */
}

/* Linux filesystem data type GUID, on-disk byte order:
   0FC63DAF-8483-4772-8E79-3D69D8477DE4  =>
   AF 3D C6 0F 83 84 72 47 8E 79 3D 69 D8 47 7D E4
*/
static const uint8_t TYPE_LINUXFS[16] = {
    0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4
};

static const uint8_t *type_guid_for(const char *type){
    if (!type) return NULL;
    if (strcasecmp(type, "linuxfs")==0 || strcasecmp(type,"linux")==0) return TYPE_LINUXFS;
    return NULL;
}

/* Resolve a target device name to a diskio key/path.
   Order: vblk row (dev/name) -> diskio_resolve(path/devkey) */
static const char *resolve_key_or_path(const char *target, char *buf, size_t bufsz) {
    const vblk_t *vb = vblk_by_name(target);
    if (vb) {
        const char *key = vb->dev[0] ? vb->dev : vb->name;
        snprintf(buf, bufsz, "%s", key);
        return buf;
    }
    const char *path = diskio_resolve(target);
    return path; // may be NULL for unmapped devkeys like "/dev/a"
}

static inline bool pread_bytes(const char *key, uint64_t off, void *dst, uint32_t len){
    return diskio_pread(key, off, dst, len);
}
static inline bool pread_lba512(const char *key, uint64_t lba, void *dst){
    return pread_bytes(key, lba*(uint64_t)LSEC, dst, LSEC);
}
static inline bool pwrite_bytes(const char *key, uint64_t off, const void *src, uint32_t len){
    return diskio_pwrite(key, off, src, len);
}
static inline bool pwrite_lba512(const char *key, uint64_t lba, const void *src){
    return pwrite_bytes(key, lba*(uint64_t)LSEC, src, LSEC);
}

/* ------------------------------- GPT read helpers -------------------------- */
static int gpt_read_header(const char *key, uint64_t hdr_lba, uint64_t total_lbas, gpt_hdr_t *out){
    uint8_t sec[512];
    if (!pread_lba512(key, hdr_lba, sec)) return 0;
    if (memcmp(sec, "EFI PART", 8)!=0) return 0;

    gpt_hdr_t h; memcpy(&h, sec, sizeof h);
    if (h.header_size < 92 || h.entry_size < sizeof(gpt_ent_t) ||
        h.num_entries == 0 || h.num_entries > 4096) return 0;

    if (total_lbas) {
        if (h.current_lba >= total_lbas || h.backup_lba >= total_lbas) return 0;
        if (h.last_usable_lba >= total_lbas || h.entries_lba >= total_lbas) return 0;
    }

    /* header CRC over header_size bytes with the CRC32 field zeroed (offset 16) */
    uint8_t *hdrbuf = (uint8_t*)malloc(h.header_size);
    if (!hdrbuf) return 0;
    if (h.header_size <= sizeof sec) memcpy(hdrbuf, sec, h.header_size);
    else {
        if (!pread_bytes(key, hdr_lba*(uint64_t)LSEC, hdrbuf, h.header_size)) { free(hdrbuf); return 0; }
    }
    if (h.header_size >= 20) memset(hdrbuf + 16, 0, 4);
    uint32_t calc = crc32_calc(hdrbuf, h.header_size);
    free(hdrbuf);
    if (calc != h.header_crc) return 0;

    *out = h;
    return 1;
}

static gpt_ent_t *gpt_read_entries(const char *key, const gpt_hdr_t *h){
    size_t bytes = (size_t)h->num_entries * h->entry_size;
    if (bytes == 0 || bytes > (8u*1024u*1024u)) return NULL;
    uint8_t *buf = (uint8_t*)malloc(bytes);
    if (!buf) return NULL;
    if (!pread_bytes(key, h->entries_lba*(uint64_t)LSEC, buf, (uint32_t)bytes)) { free(buf); return NULL; }
    uint32_t crc = crc32_calc(buf, bytes);
    if (crc != h->entries_crc) { free(buf); return NULL; }
    return (gpt_ent_t*)buf; /* caller frees */
}

/* ------------------------------- GPT write helpers ------------------------- */
static void gpt_update_header_crc(gpt_hdr_t *h){
    h->header_crc = 0;
    uint8_t *hdr = (uint8_t*)malloc(h->header_size);
    memcpy(hdr, h, h->header_size);
    if (h->header_size >= 20) memset(hdr+16, 0, 4);
    h->header_crc = crc32_calc(hdr, h->header_size);
    free(hdr);
}
static void gpt_update_entries_crc(gpt_hdr_t *h, const gpt_ent_t *ents){
    size_t bytes = (size_t)h->num_entries * h->entry_size;
    h->entries_crc = crc32_calc(ents, bytes);
}

static bool gpt_write_primary(const char *key, const gpt_hdr_t *h, const gpt_ent_t *ents){
    size_t bytes = (size_t)h->num_entries * h->entry_size;
    if (!pwrite_bytes(key, h->entries_lba*(uint64_t)LSEC, ents, (uint32_t)bytes)) return false;
    uint8_t sec[512]; memset(sec, 0, sizeof sec);
    memcpy(sec, h, sizeof *h);
    return pwrite_lba512(key, h->current_lba, sec);
}
static bool gpt_write_backup(const char *key, const gpt_hdr_t *h_bak, const gpt_ent_t *ents){
    size_t bytes = (size_t)h_bak->num_entries * h_bak->entry_size;
    if (!pwrite_bytes(key, h_bak->entries_lba*(uint64_t)LSEC, ents, (uint32_t)bytes)) return false;
    uint8_t sec[512]; memset(sec, 0, sizeof sec);
    memcpy(sec, h_bak, sizeof *h_bak);
    return pwrite_lba512(key, h_bak->current_lba, sec);
}

/* Protective MBR at LBA0 (single 0xEE partition covering the disk) */
static bool write_protective_mbr(const char *key, uint64_t total_lbas){
    uint8_t mbr[512]; memset(mbr, 0, sizeof mbr);
    uint8_t *e = mbr + 446;
    e[4] = 0xEE;
    uint32_t start = 1;
    uint32_t count = (total_lbas > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)total_lbas - 1u;
    e[8]  = (uint8_t)(start & 0xFF);
    e[9]  = (uint8_t)((start >> 8) & 0xFF);
    e[10] = (uint8_t)((start >> 16) & 0xFF);
    e[11] = (uint8_t)((start >> 24) & 0xFF);
    e[12] = (uint8_t)(count & 0xFF);
    e[13] = (uint8_t)((count >> 8) & 0xFF);
    e[14] = (uint8_t)((count >> 16) & 0xFF);
    e[15] = (uint8_t)((count >> 24) & 0xFF);
    mbr[510]=0x55; mbr[511]=0xAA;
    return pwrite_lba512(key, 0, mbr);
}

/* ------------------------------- size parsing ------------------------------ */
/* Parses strings like: "2048s", "1MiB", "512KB", "100%", "4096", etc.
   - returns either bytes (out_bytes) or a percentage (out_pct in 0..100) or sectors (out_sectors).
   - Only one of out_pct or out_sectors will be nonzero (if applicable). */
static bool parse_size_spec(const char *s, uint64_t *out_bytes, uint32_t *out_pct, uint64_t *out_sectors){
    if (!s || !*s) return false;
    while (*s==' ' || *s=='\t') ++s;

    size_t len = strlen(s);
    // percentage?
    if (len && s[len-1]=='%') {
        char *end=NULL;
        long p = strtol(s, &end, 10);
        if (end && *end=='%' && p>=0 && p<=100) {
            if (out_pct) *out_pct = (uint32_t)p;
            if (out_bytes) *out_bytes = 0;
            if (out_sectors) *out_sectors = 0;
            return true;
        }
        return false;
    }

    // sectors suffix 's' or 'S'
    if (len && (s[len-1]=='s' || s[len-1]=='S')) {
        char *end=NULL;
        uint64_t v = strtoull(s, &end, 10);
        if (end && (*end=='s' || *end=='S')) {
            if (out_sectors) *out_sectors = v;
            if (out_bytes) *out_bytes = 0;
            if (out_pct) *out_pct = 0;
            return true;
        }
        return false;
    }

    // bytes with unit
    // normalize a small copy
    char tmp[64]; snprintf(tmp, sizeof tmp, "%s", s);
    for (char *p=tmp; *p; ++p) *p=(char)tolower((unsigned char)*p);

    uint64_t mul = 1;
    if (strstr(tmp, "kib")) mul = 1024ull;
    else if (strstr(tmp, "kb")) mul = 1000ull;
    else if (strstr(tmp, "mib")) mul = 1024ull*1024ull;
    else if (strstr(tmp, "mb"))  mul = 1000ull*1000ull;
    else if (strstr(tmp, "gib")) mul = 1024ull*1024ull*1024ull;
    else if (strstr(tmp, "gb"))  mul = 1000ull*1000ull*1000ull;
    else {
        // maybe plain number: treat as bytes
        mul = 1ull;
    }

    // strip non-digits for strtoull
    char num[64]={0}; size_t j=0;
    for (size_t i=0; tmp[i] && j+1<sizeof num; ++i) {
        if ((tmp[i]>='0' && tmp[i]<='9')) num[j++]=tmp[i];
    }
    if (j==0) return false;
    uint64_t v = strtoull(num, NULL, 10);
    if (out_bytes) *out_bytes = v * mul;
    if (out_pct) *out_pct = 0;
    if (out_sectors) *out_sectors = 0;
    return true;
}

/* ------------------------------- subcommands ------------------------------- */

static int gpt_cmd_print(const char *target) {
    char keybuf[64];
    const char *key = resolve_key_or_path(target, keybuf, sizeof keybuf);
    if (!key) {
        fprintf(stderr, "gpt print: cannot resolve \"%s\" (use -i <image> %s first, or pass a path)\n",
                target, target);
        return 0;
    }

    uint64_t size_bytes = diskio_size_bytes(key);
    uint64_t total_lbas = size_bytes ? (size_bytes / (uint64_t)LSEC) : 0;

    gpt_hdr_t h;
    if (!gpt_read_header(key, 1, total_lbas, &h)) {
        printf("No GPT found on %s\n", target);
        return 0;
    }

    printf("Disk: %s  Sector: 512\n", target);
    printf("Disk GUID: "); print_guid(h.disk_guid); printf("\n");
    printf("Primary GPT: LBA %" PRIu64 " | Array: LBA %" PRIu64 "  (entries=%u, size=%u)\n",
           h.current_lba, h.entries_lba, h.num_entries, h.entry_size);
    printf("Backup  GPT: LBA %" PRIu64 "\n\n", h.backup_lba);

    gpt_ent_t *ents = gpt_read_entries(key, &h);
    if (!ents) {
        printf("(Could not read entries or CRC mismatch)\n");
        return 0;
    }

    printf("Idx  Start LBA     End LBA       Size        Type        Name\n");
    printf("---  ------------  ------------  ----------  ----------  ----------------\n");
    unsigned idx=1;
    for (uint32_t i=0; i<h.num_entries; ++i) {
        gpt_ent_t *e = (gpt_ent_t*)((uint8_t*)ents + (size_t)i*h.entry_size);
        bool empty=true; for (int k=0;k<16;k++) if (e->type_guid[k]) { empty=false; break; }
        if (empty) continue;

        uint64_t first = e->first_lba;
        uint64_t last  = e->last_lba;
        uint64_t blks  = (last >= first) ? (last - first + 1) : 0;
        double   mb    = (double)blks * (double)LSEC / (1024.0*1024.0);

        char name[64]; from_utf16le(e->name_utf16, name, sizeof name);

        const char *type = "unknown";
        if (memcmp(e->type_guid, TYPE_LINUXFS, 16)==0) type="linuxfs";

        printf("%3u  %12" PRIu64 "  %12" PRIu64 "  %10.1f  %-10s  %-16s\n",
               idx++, first, last, mb, type, name);
    }
    free(ents);
    return 0;
}

/* KEEPING ORIGINAL NAME: 'gpt init' */
static int gpt_cmd_init(const char *target) {
    char keybuf[64];
    const char *key = resolve_key_or_path(target, keybuf, sizeof keybuf);
    if (!key) {
        fprintf(stderr, "gpt init: cannot resolve \"%s\"\n", target);
        return 0;
    }

    uint64_t size_bytes = diskio_size_bytes(key);
    if (size_bytes < (uint64_t)(LSEC * (2 + ENTRIES_SECTORS + 1))) {
        fprintf(stderr, "gpt init: image too small (need at least %" PRIu64 " bytes)\n",
                (uint64_t)LSEC*(2+ENTRIES_SECTORS+1));
        return 0;
    }
    uint64_t total_lbas = size_bytes / (uint64_t)LSEC;

    uint64_t primary_hdr_lba = 1;
    uint64_t primary_ent_lba = 2;
    uint64_t backup_hdr_lba  = total_lbas - 1;
    uint64_t backup_ent_lba  = backup_hdr_lba - ENTRIES_SECTORS;

    uint64_t first_usable = primary_ent_lba + ENTRIES_SECTORS;     // typically 34
    uint64_t last_usable  = backup_ent_lba - 1;

    /* zero the regions (optional) */
    uint8_t zer[512]; memset(zer, 0, sizeof zer);
    for (uint64_t l=primary_ent_lba; l<primary_ent_lba+ENTRIES_SECTORS; ++l) pwrite_lba512(key, l, zer);
    for (uint64_t l=backup_ent_lba;  l<backup_ent_lba +ENTRIES_SECTORS;  ++l) pwrite_lba512(key, l, zer);
    pwrite_lba512(key, primary_hdr_lba, zer);
    pwrite_lba512(key, backup_hdr_lba,  zer);

    gpt_ent_t *ents = (gpt_ent_t*)calloc(1, ENTRIES_BYTES);
    if (!ents) { fprintf(stderr, "gpt init: alloc entries failed\n"); return 0; }

    uint8_t disk_guid[16]; gen_guid(disk_guid);

    gpt_hdr_t hp; memset(&hp, 0, sizeof hp);
    memcpy(hp.sig, "EFI PART", 8);
    hp.rev = 0x00010000u;
    hp.header_size = HDR_SIZE;
    hp.current_lba = 1;
    hp.backup_lba  = backup_hdr_lba;
    hp.first_usable_lba = first_usable;
    hp.last_usable_lba  = last_usable;
    memcpy(hp.disk_guid, disk_guid, 16);
    hp.entries_lba = primary_ent_lba;
    hp.num_entries = ENTRIES_MAX;
    hp.entry_size  = ENTRY_SIZE;
    gpt_update_entries_crc(&hp, ents);
    gpt_update_header_crc(&hp);

    gpt_hdr_t hb = hp;
    hb.current_lba = backup_hdr_lba;
    hb.backup_lba  = 1;
    hb.entries_lba = backup_ent_lba;
    gpt_update_entries_crc(&hb, ents);
    gpt_update_header_crc(&hb);

    if (!write_protective_mbr(key, total_lbas)) {
        fprintf(stderr, "gpt init: failed to write protective MBR\n");
        free(ents); return 0;
    }
    if (!gpt_write_primary(key, &hp, ents) || !gpt_write_backup(key, &hb, ents)) {
        fprintf(stderr, "gpt init: failed to write GPT structures\n");
        free(ents); return 0;
    }
    free(ents);

    printf("Initialized GPT on %s (primary LBA=%" PRIu64 ", backup LBA=%" PRIu64 ")\n",
           target, (uint64_t)1, backup_hdr_lba);

    (void)block_rescan(target);
    return 0;
}

/* ---- shared add logic (given absolute start_lba/end_lba) ---- */
static int gpt_add_by_range(const char *target, const char *type, const char *name,
                            uint64_t first_lba, uint64_t last_lba)
{
    const uint8_t *type_guid = type_guid_for(type);
    if (!type_guid) { fprintf(stderr, "gpt add: unknown type \"%s\"\n", type?type:"(null)"); return 0; }
    if (last_lba < first_lba) { fprintf(stderr, "gpt add: end before start\n"); return 0; }

    char keybuf[64];
    const char *key = resolve_key_or_path(target, keybuf, sizeof keybuf);
    if (!key) { fprintf(stderr, "gpt add: cannot resolve \"%s\"\n", target); return 0; }

    uint64_t size_bytes = diskio_size_bytes(key);
    uint64_t total_lbas = size_bytes ? (size_bytes / (uint64_t)LSEC) : 0;

    gpt_hdr_t hp;
    if (!gpt_read_header(key, 1, total_lbas, &hp)) {
        fprintf(stderr, "gpt add: no GPT on %s (run 'gpt init %s')\n", target, target);
        return 0;
    }
    gpt_hdr_t hb;
    if (!gpt_read_header(key, hp.backup_lba, total_lbas, &hb)) {
        fprintf(stderr, "gpt add: backup GPT invalid\n"); return 0;
    }

    gpt_ent_t *ents = gpt_read_entries(key, &hp);
    if (!ents) {
        size_t bytes = (size_t)hb.num_entries * hb.entry_size;
        ents = (gpt_ent_t*)malloc(bytes);
        if (!ents) { fprintf(stderr, "gpt add: alloc fail\n"); return 0; }
        if (!pread_bytes(key, hb.entries_lba*(uint64_t)LSEC, ents, (uint32_t)bytes)) {
            free(ents); fprintf(stderr, "gpt add: cannot read entries\n"); return 0;
        }
    }

    /* clamp to usable area */
    if (first_lba < hp.first_usable_lba) first_lba = hp.first_usable_lba;
    if (last_lba  > hp.last_usable_lba)  last_lba  = hp.last_usable_lba;
    if (last_lba < first_lba) { fprintf(stderr, "gpt add: range outside usable area\n"); free(ents); return 0; }

    /* TODO: optional overlap check with existing entries */

    /* find free slot */
    uint32_t idx = UINT32_MAX;
    for (uint32_t i=0;i<hp.num_entries;i++){
        gpt_ent_t *e = (gpt_ent_t*)((uint8_t*)ents + (size_t)i*hp.entry_size);
        bool empty=true; for (int k=0;k<16;k++) if (e->type_guid[k]) { empty=false; break; }
        if (empty) { idx=i; break; }
    }
    if (idx==UINT32_MAX) { fprintf(stderr, "gpt add: no free entries (max=%u)\n", hp.num_entries); free(ents); return 0; }

    gpt_ent_t *e = (gpt_ent_t*)((uint8_t*)ents + (size_t)idx*hp.entry_size);
    memset(e, 0, hp.entry_size);
    memcpy(e->type_guid, type_guid, 16);
    gen_guid(e->part_guid);
    e->first_lba = first_lba;
    e->last_lba  = last_lba;
    to_utf16le(name ? name : "", e->name_utf16);

    /* update CRCs + write primary+backup */
    gpt_update_entries_crc(&hp, ents);
    gpt_update_header_crc(&hp);
    if (!gpt_write_primary(key, &hp, ents)) { fprintf(stderr, "gpt add: failed to write primary\n"); free(ents); return 0; }

    gpt_update_entries_crc(&hb, ents);
    gpt_update_header_crc(&hb);
    if (!gpt_write_backup(key, &hb, ents)) { fprintf(stderr, "gpt add: failed to write backup\n"); free(ents); return 0; }

    free(ents);

    printf("Added %s '%s' at [%" PRIu64 ", %" PRIu64 "] on %s (entry #%u)\n",
           type ? type : "partition", name ? name : "", first_lba, last_lba, target, (unsigned)(idx+1));

    (void)block_rescan(target);
    return 0;
}

/* ---- option form: --type,--name,--start,--size/--end ---- */
static int gpt_cmd_add_opts(int argc, char **argv){
    /* argv[0]="gpt", argv[1]="add", argv[2]=<dev>, argv[3..] options */
    const char *dev  = argv[2];
    const char *type = NULL;
    const char *name = NULL;
    const char *start_s = NULL;
    const char *size_s  = NULL;
    const char *end_s   = NULL;

    for (int i=3; i<argc; ++i){
        const char *a = argv[i];
        if      (strcmp(a, "--type")==0 && i+1<argc) { type = argv[++i]; }
        else if (strcmp(a, "--name")==0 && i+1<argc) { name = argv[++i]; }
        else if (strcmp(a, "--start")==0 && i+1<argc){ start_s = argv[++i]; }
        else if (strcmp(a, "--size")==0 && i+1<argc) { size_s = argv[++i]; }
        else if (strcmp(a, "--end")==0 && i+1<argc)  { end_s   = argv[++i]; }
        else {
            fprintf(stderr, "gpt add: unknown or incomplete option '%s'\n", a);
            return 0;
        }
    }
    if (!dev || !type || !name || !start_s || (!size_s && !end_s)) {
        fprintf(stderr, "gpt add: required options: --type <t> --name <n> --start <spec> (--size <spec>|--end <spec>)\n");
        return 0;
    }

    char keybuf[64];
    const char *key = resolve_key_or_path(dev, keybuf, sizeof keybuf);
    if (!key) { fprintf(stderr, "gpt add: cannot resolve '%s'\n", dev); return 0; }

    uint64_t size_bytes = diskio_size_bytes(key);
    uint64_t total_lbas = size_bytes ? (size_bytes/(uint64_t)LSEC) : 0;

    /* read primary header to get usable range */
    gpt_hdr_t h;
    if (!gpt_read_header(key, 1, total_lbas, &h)) {
        fprintf(stderr, "gpt add: no GPT on %s (run 'gpt init %s')\n", dev, dev);
        return 0;
    }

    /* compute start LBA */
    uint64_t start_bytes=0, start_sectors=0;
    uint32_t start_pct=0;
    if (!parse_size_spec(start_s, &start_bytes, &start_pct, &start_sectors)) {
        fprintf(stderr, "gpt add: bad --start '%s'\n", start_s); return 0;
    }

    uint64_t start_lba = 0;
    if (start_sectors) {
        start_lba = start_sectors;                  /* absolute sectors */
    } else if (start_pct) {
        /* percent of usable span offset from first_usable */
        uint64_t span = (h.last_usable_lba >= h.first_usable_lba)
                      ? (h.last_usable_lba - h.first_usable_lba + 1) : 0;
        start_lba = h.first_usable_lba + (span * start_pct) / 100u;
    } else {
        /* bytes from LBA0 */
        start_lba = start_bytes / (uint64_t)LSEC;
    }

    /* clamp start to usable start */
    if (start_lba < h.first_usable_lba) start_lba = h.first_usable_lba;

    /* compute end LBA */
    uint64_t end_lba = 0;
    if (end_s) {
        uint64_t end_bytes=0, end_sectors=0; uint32_t end_pct=0;
        if (!parse_size_spec(end_s, &end_bytes, &end_pct, &end_sectors)) {
            fprintf(stderr, "gpt add: bad --end '%s'\n", end_s); return 0;
        }
        if (end_sectors) end_lba = end_sectors;
        else if (end_pct) {
            uint64_t span = (h.last_usable_lba >= h.first_usable_lba)
                          ? (h.last_usable_lba - h.first_usable_lba + 1) : 0;
            end_lba = h.first_usable_lba + (span * end_pct)/100u;
        } else {
            /* end specified as absolute byte offset -> convert to lba index (inclusive) */
            uint64_t elba = end_bytes / (uint64_t)LSEC;
            end_lba = (elba>0) ? (elba-1) : 0;   /* interpret as last occupied sector */
        }
    } else {
        /* --size path */
        uint64_t sz_bytes=0, sz_sectors=0; uint32_t sz_pct=0;
        if (!parse_size_spec(size_s, &sz_bytes, &sz_pct, &sz_sectors)) {
            fprintf(stderr, "gpt add: bad --size '%s'\n", size_s); return 0;
        }
        if (sz_sectors) {
            end_lba = start_lba + sz_sectors - 1;
        } else if (sz_pct) {
            uint64_t rem = (start_lba <= h.last_usable_lba)
                         ? (h.last_usable_lba - start_lba + 1) : 0;
            end_lba = start_lba + (rem * sz_pct)/100u - 1;
        } else {
            uint64_t nsec = (sz_bytes + (uint64_t)LSEC - 1) / (uint64_t)LSEC; /* ceil */
            end_lba = start_lba + (nsec? nsec:1) - 1;
        }
    }

    /* clamp end to usable end */
    if (end_lba > h.last_usable_lba) end_lba = h.last_usable_lba;
    if (end_lba < start_lba) { fprintf(stderr, "gpt add: computed empty/negative range\n"); return 0; }

    return gpt_add_by_range(dev, type, name, start_lba, end_lba);
}

/* ---- legacy positional form: gpt add <dev> <type> <name> <first> <last> ---- */
static int gpt_cmd_add_legacy(const char *dev, const char *type, const char *name,
                              uint64_t first_lba, uint64_t last_lba)
{
    return gpt_add_by_range(dev, type, name, first_lba, last_lba);
}

/* ------------------------------- usage/dispatcher -------------------------- */
static void usage(void){
    printf(
      "gpt commands:\n"
      "  gpt print <dev>                          # show GPT header and entries\n"
      "  gpt init  <dev>                          # create protective MBR + GPT (empty)\n"
      "  gpt add <dev> <type> <name> <first> <last>\n"
      "  gpt add <dev> --type <t> --name <n> --start <spec> [--size <spec> | --end <spec>]\n"
      "    size/start spec examples: 2048s | 1MiB | 64MB | 100%%\n"
      "Supported types: linuxfs\n"
    );
}

int cmd_gpt(int argc, char **argv){
    if (argc < 2) { usage(); return 0; }

    const char *sub = argv[1];

    if (strcmp(sub, "print")==0) {
        if (argc < 3) { usage(); return 0; }
        return gpt_cmd_print(argv[2]);
    }

    if (strcmp(sub, "init")==0) {
        if (argc < 3) { usage(); return 0; }
        return gpt_cmd_init(argv[2]);
    }

    if (strcmp(sub, "add")==0) {
        if (argc < 3) { usage(); return 0; }
        /* option form if any argument after <dev> starts with "--" */
        bool has_opts = false;
        for (int i=3; i<argc; ++i) if (strncmp(argv[i], "--", 2)==0) { has_opts=true; break; }
        if (has_opts) return gpt_cmd_add_opts(argc, argv);

        if (argc < 7) { usage(); return 0; }
        const char *dev  = argv[2];
        const char *type = argv[3];
        const char *name = argv[4];
        uint64_t first = strtoull(argv[5], NULL, 0);
        uint64_t last  = strtoull(argv[6], NULL, 0);
        return gpt_cmd_add_legacy(dev, type, name, first, last);
    }

    usage();
    return 0; /* never kill the REPL */
}
