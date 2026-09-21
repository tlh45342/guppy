#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "fs_format.h"

#define NTFS_OEM "NTFS    "
#define BOOT_JMP0 0xEB
#define BOOT_JMP1 0x52
#define BOOT_JMP2 0x90

#define NTFS_ATTR_STANDARD_INFORMATION 0x10u
#define NTFS_ATTR_FILE_NAME            0x30u
#define NTFS_ATTR_VOLUME_NAME          0x60u
#define NTFS_ATTR_VOLUME_INFORMATION   0x70u
#define NTFS_ATTR_DATA                 0x80u
#define NTFS_ATTR_INDEX_ROOT           0x90u
#define NTFS_ATTR_END                  0xFFFFFFFFu
#define NTFS_FILE_IN_USE               0x0001u
#define NTFS_FILE_DIRECTORY            0x0002u
#define NTFS_FILE_ATTR_HIDDEN          0x00000002u
#define NTFS_FILE_ATTR_SYSTEM          0x00000004u
#define NTFS_FILE_ATTR_DIRECTORY       0x10000000u
#define NTFS_INDEX_ENTRY_LAST           0x0002u
#define NTFS_COLLATION_FILE_NAME        0x00000001u

#pragma pack(push,1)
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bps;
    uint8_t  spc;
    uint16_t rsvd;
    uint8_t  zero1[5];
    uint8_t  media;
    uint8_t  zero2[2];
    uint16_t spt;
    uint16_t heads;
    uint32_t hidden;
    uint32_t unused;
    uint32_t unused2;
    uint64_t totsec;
    uint64_t mft_clus;
    uint64_t mftmirr_clus;
    int8_t   clus_per_mft;
    uint8_t  pad1[3];
    int8_t   clus_per_index;
    uint8_t  pad2[3];
    uint64_t serial;
    uint32_t bscsum;
    uint8_t  bootstrap[426];
    uint16_t sig55aa;
} ntfs_bpb_t;

typedef struct {
    char     magic[4];
    uint16_t usa_ofs;
    uint16_t usa_count;
    uint64_t lsn;
    uint16_t seq_no;
    uint16_t hardlinks;
    uint16_t attr_ofs;
    uint16_t flags;
    uint32_t bytes_real;
    uint32_t bytes_alloc;
    uint64_t base_file;
    uint16_t next_attr_id;
    uint16_t align;
    uint32_t mft_record_no;
} ntfs_file_rec_t;

typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t  nonresident;
    uint8_t  name_len;
    uint16_t name_ofs;
    uint16_t flags;
    uint16_t attr_id;
    uint32_t value_len;
    uint16_t value_ofs;
    uint8_t  resident_flags;
    uint8_t  reserved;
} ntfs_resident_attr_t;

typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t  nonresident;
    uint8_t  name_len;
    uint16_t name_ofs;
    uint16_t flags;
    uint16_t attr_id;
    uint64_t lowest_vcn;
    uint64_t highest_vcn;
    uint16_t run_ofs;
    uint16_t compression_unit;
    uint32_t reserved;
    uint64_t allocated_size;
    uint64_t data_size;
    uint64_t initialized_size;
} ntfs_nonresident_attr_t;
#pragma pack(pop)

typedef struct {
    uint8_t buf[1024];
    uint32_t rec_bytes;
    uint32_t sector_bytes;
    uint16_t usa_ofs;
    uint16_t usa_count;
    uint16_t attr_cursor;
    uint16_t next_attr_id;
    uint32_t rec_no;
} ntfs_record_builder_t;

static uint64_t fsize(FILE *f){
    long cur=ftell(f); fseek(f,0,SEEK_END); long end=ftell(f); fseek(f,cur,SEEK_SET);
    return (uint64_t)end;
}

static void zero_region(FILE *f, uint64_t off, uint64_t len){
    static uint8_t z[4096]={0};
    fseek(f,(long)off,SEEK_SET);
    while (len){
        size_t n=(len>sizeof z)? sizeof z : (size_t)len;
        fwrite(z,1,n,f);
        len -= n;
    }
}

static void put_sector(FILE *f, uint32_t bps, uint64_t lba, const void *buf, size_t len){
    fseek(f,(long)((lba)*bps),SEEK_SET);
    fwrite(buf,1,len,f);
}

static uint16_t align8_u16(uint16_t v){ return (uint16_t)((v + 7u) & ~7u); }

static void put_u16(uint8_t *p, uint16_t v){ memcpy(p, &v, sizeof v); }
static void put_u32(uint8_t *p, uint32_t v){ memcpy(p, &v, sizeof v); }
static void put_u64(uint8_t *p, uint64_t v){ memcpy(p, &v, sizeof v); }

typedef struct {
    uint32_t bps, spc;
    uint64_t total_sectors;
    uint64_t lba_off;
    uint64_t mft_lcn;
    uint64_t mftmirr_lcn;
    uint32_t bytes_per_cluster;
    uint32_t bytes_per_mftrec;
} layout_t;

typedef struct ntfs_index_seed {
    uint32_t rec_no;
    const char *name;
    uint32_t file_attrs;
    bool directory;
} ntfs_index_seed_t;

static void plan_layout(const mkfs_ntfs_opts_t *opt, uint64_t totsec, layout_t *L){
    memset(L,0,sizeof *L);
    L->bps = opt->bytes_per_sec ? opt->bytes_per_sec : 512;
    L->spc = opt->sec_per_clus ? opt->sec_per_clus : 8;
    L->bytes_per_cluster = L->bps * L->spc;
    L->bytes_per_mftrec = 1024;
    L->total_sectors = totsec;
    L->lba_off = opt->lba_offset;
    L->mft_lcn = opt->mft_start_clus ? opt->mft_start_clus : 4;
    L->mftmirr_lcn = opt->mftmirr_clus ? opt->mftmirr_clus : 8;
}

static void write_boot_sector(FILE *f, const layout_t *L, const mkfs_ntfs_opts_t *opt){
    (void)opt;
    ntfs_bpb_t b; memset(&b,0,sizeof b);
    b.jmp[0]=BOOT_JMP0; b.jmp[1]=BOOT_JMP1; b.jmp[2]=BOOT_JMP2;
    memcpy(b.oem, NTFS_OEM, 8);
    b.bps = (uint16_t)L->bps;
    b.spc = (uint8_t)L->spc;
    b.media = 0xF8;
    b.spt = 32;
    b.heads = 64;
    b.hidden = (uint32_t)L->lba_off;
    b.totsec = L->total_sectors;
    b.mft_clus = L->mft_lcn;
    b.mftmirr_clus = L->mftmirr_lcn;
    b.clus_per_mft = -10;
    b.clus_per_index = 1;
    b.serial = 0x1122334455667788ULL;
    b.sig55aa = 0xAA55;
    put_sector(f, L->bps, L->lba_off, &b, sizeof b);
    put_sector(f, L->bps, L->lba_off + (L->total_sectors - 1), &b, sizeof b);
}

static bool ntfs_record_init(ntfs_record_builder_t *b, const layout_t *L,
                             uint32_t rec_no, bool directory)
{
    if (!b || L->bytes_per_mftrec != sizeof b->buf || L->bps == 0 ||
        sizeof b->buf % L->bps != 0) return false;
    memset(b, 0, sizeof *b);
    b->rec_bytes = (uint32_t)sizeof b->buf;
    b->sector_bytes = L->bps;
    b->usa_ofs = 0x30;
    b->usa_count = (uint16_t)(1u + b->rec_bytes / b->sector_bytes);
    b->attr_cursor = align8_u16((uint16_t)(b->usa_ofs + b->usa_count * 2u));
    b->next_attr_id = 0;
    b->rec_no = rec_no;

    ntfs_file_rec_t *fr = (ntfs_file_rec_t*)b->buf;
    memcpy(fr->magic, "FILE", 4);
    fr->usa_ofs = b->usa_ofs;
    fr->usa_count = b->usa_count;
    fr->seq_no = 1;
    fr->hardlinks = 1;
    fr->attr_ofs = b->attr_cursor;
    fr->flags = NTFS_FILE_IN_USE | (directory ? NTFS_FILE_DIRECTORY : 0);
    fr->bytes_alloc = b->rec_bytes;
    fr->mft_record_no = rec_no;
    return true;
}

static bool ntfs_record_add_resident_attr(ntfs_record_builder_t *b, uint32_t type,
                                          const void *value, uint32_t value_len)
{
    const uint16_t value_ofs = (uint16_t)sizeof(ntfs_resident_attr_t);
    uint32_t raw_len = (uint32_t)value_ofs + value_len;
    uint32_t attr_len = (raw_len + 7u) & ~7u;
    if ((uint32_t)b->attr_cursor + attr_len + 8u > b->rec_bytes) return false;

    ntfs_resident_attr_t a;
    memset(&a, 0, sizeof a);
    a.type = type;
    a.length = attr_len;
    a.attr_id = b->next_attr_id++;
    a.value_len = value_len;
    a.value_ofs = value_ofs;

    memcpy(b->buf + b->attr_cursor, &a, sizeof a);
    if (value_len) memcpy(b->buf + b->attr_cursor + value_ofs, value, value_len);
    b->attr_cursor = (uint16_t)(b->attr_cursor + attr_len);
    return true;
}

static size_t ntfs_encode_single_run(uint8_t *out, size_t cap, uint64_t clusters, int64_t lcn_delta)
{
    if (!out || cap < 4 || clusters == 0 || lcn_delta < 0) return 0;
    unsigned len_bytes = 1, off_bytes = 1;
    while (len_bytes < 8 && (clusters >> (len_bytes * 8u)) != 0) ++len_bytes;
    while (off_bytes < 8 && ((uint64_t)lcn_delta >> (off_bytes * 8u)) != 0) ++off_bytes;
    if (1u + len_bytes + off_bytes + 1u > cap) return 0;
    out[0] = (uint8_t)((off_bytes << 4) | len_bytes);
    for (unsigned i=0; i<len_bytes; ++i) out[1+i] = (uint8_t)(clusters >> (i*8u));
    for (unsigned i=0; i<off_bytes; ++i) out[1+len_bytes+i] = (uint8_t)((uint64_t)lcn_delta >> (i*8u));
    out[1+len_bytes+off_bytes] = 0;
    return 2u + len_bytes + off_bytes;
}

static bool ntfs_record_add_nonresident_data(ntfs_record_builder_t *b,
                                              uint64_t start_lcn, uint64_t clusters,
                                              uint64_t allocated_size, uint64_t data_size,
                                              uint64_t initialized_size)
{
    uint8_t run[24];
    size_t run_len = ntfs_encode_single_run(run, sizeof run, clusters, (int64_t)start_lcn);
    if (!run_len) return false;
    const uint16_t run_ofs = (uint16_t)sizeof(ntfs_nonresident_attr_t);
    uint32_t raw_len = (uint32_t)run_ofs + (uint32_t)run_len;
    uint32_t attr_len = (raw_len + 7u) & ~7u;
    if ((uint32_t)b->attr_cursor + attr_len + 8u > b->rec_bytes) return false;

    ntfs_nonresident_attr_t a;
    memset(&a, 0, sizeof a);
    a.type = NTFS_ATTR_DATA;
    a.length = attr_len;
    a.nonresident = 1;
    a.attr_id = b->next_attr_id++;
    a.lowest_vcn = 0;
    a.highest_vcn = clusters - 1u;
    a.run_ofs = run_ofs;
    a.allocated_size = allocated_size;
    a.data_size = data_size;
    a.initialized_size = initialized_size;
    memcpy(b->buf + b->attr_cursor, &a, sizeof a);
    memcpy(b->buf + b->attr_cursor + run_ofs, run, run_len);
    b->attr_cursor = (uint16_t)(b->attr_cursor + attr_len);
    return true;
}

static uint32_t make_file_name(uint8_t *value, size_t cap, uint64_t parent_ref,
                               const char *name, uint32_t file_attrs,
                               bool directory);

static bool ntfs_record_add_index_root(ntfs_record_builder_t *b, const layout_t *L,
                                       const ntfs_index_seed_t *entries, size_t entry_count)
{
    /* INDEX_ROOT value = INDEX_ROOT header (16) + INDEX_HEADER (16) + entries.
       The key of each entry is a resident FILE_NAME value.  This pass keeps the
       root index small enough to remain resident; INDEX_ALLOCATION comes later. */
    uint8_t value[768];
    memset(value, 0, sizeof value);
    put_u32(value + 0, NTFS_ATTR_FILE_NAME);          /* indexed attribute type */
    put_u32(value + 4, NTFS_COLLATION_FILE_NAME);     /* filename collation */
    put_u32(value + 8, L->bytes_per_cluster);         /* index block size */
    value[12] = 1;                                    /* clusters/index block */

    const uint32_t index_hdr = 16;
    const uint32_t entries_ofs = 16;                  /* from INDEX_HEADER */
    uint32_t cursor = index_hdr + entries_ofs;

    for (size_t i=0; i<entry_count; ++i) {
        uint8_t key[66 + 255*2];
        uint32_t key_len = make_file_name(key, sizeof key, 5, entries[i].name,
                                          entries[i].file_attrs, entries[i].directory);
        if (!key_len) return false;
        uint32_t entry_len = (16u + key_len + 7u) & ~7u;
        if (cursor + entry_len + 16u > sizeof value) return false;

        uint64_t file_ref = ((uint64_t)1u << 48) | entries[i].rec_no;
        put_u64(value + cursor + 0, file_ref);
        put_u16(value + cursor + 8, (uint16_t)entry_len);
        put_u16(value + cursor + 10, (uint16_t)key_len);
        put_u32(value + cursor + 12, 0);
        memcpy(value + cursor + 16, key, key_len);
        cursor += entry_len;
    }

    /* Terminal INDEX_ENTRY. */
    if (cursor + 16u > sizeof value) return false;
    put_u64(value + cursor + 0, 0);
    put_u16(value + cursor + 8, 16);
    put_u16(value + cursor + 10, 0);
    put_u32(value + cursor + 12, NTFS_INDEX_ENTRY_LAST);
    cursor += 16u;

    put_u32(value + index_hdr + 0, entries_ofs);
    put_u32(value + index_hdr + 4, cursor - index_hdr); /* total bytes in index hdr+entries */
    put_u32(value + index_hdr + 8, cursor - index_hdr); /* resident allocation */
    value[index_hdr + 12] = 0;                          /* small index, no children */

    return ntfs_record_add_resident_attr(b, NTFS_ATTR_INDEX_ROOT, value, cursor);
}

static void make_standard_information(uint8_t value[48], uint32_t file_attrs){
    memset(value, 0, 48);
    /* Four NTFS timestamps remain zero for now.  The structural fields are real. */
    put_u32(value + 32, file_attrs);
    put_u32(value + 36, 0); /* max versions */
    put_u32(value + 40, 0); /* version number */
    put_u32(value + 44, 0); /* class id */
}

static uint32_t make_file_name(uint8_t *value, size_t cap, uint64_t parent_ref,
                               const char *name, uint32_t file_attrs,
                               bool directory)
{
    size_t n = strlen(name);
    if (n > 255 || cap < 66u + n * 2u) return 0;
    memset(value, 0, 66u + n * 2u);
    put_u64(value + 0, parent_ref);
    /* timestamps [8..39] are zero for now */
    put_u64(value + 40, 0); /* allocated size: metadata name entry only */
    put_u64(value + 48, 0); /* real size */
    put_u32(value + 56, file_attrs | (directory ? NTFS_FILE_ATTR_DIRECTORY : 0));
    put_u32(value + 60, 0); /* reparse / EA */
    value[64] = (uint8_t)n;
    value[65] = 1; /* Win32 namespace */
    for (size_t i=0; i<n; ++i) {
        value[66 + i*2] = (uint8_t)name[i];
        value[66 + i*2 + 1] = 0;
    }
    return (uint32_t)(66u + n * 2u);
}

static bool ntfs_record_add_standard_information(ntfs_record_builder_t *b,
                                                  uint32_t file_attrs)
{
    uint8_t value[48];
    make_standard_information(value, file_attrs);
    return ntfs_record_add_resident_attr(b, NTFS_ATTR_STANDARD_INFORMATION,
                                         value, sizeof value);
}

static bool ntfs_record_add_file_name(ntfs_record_builder_t *b, uint64_t parent_ref,
                                      const char *name, uint32_t file_attrs,
                                      bool directory)
{
    uint8_t value[66 + 255*2];
    uint32_t len = make_file_name(value, sizeof value, parent_ref, name,
                                  file_attrs, directory);
    if (!len) return false;
    return ntfs_record_add_resident_attr(b, NTFS_ATTR_FILE_NAME, value, len);
}

static bool ntfs_record_add_volume_metadata(ntfs_record_builder_t *b)
{
    /* $VOLUME_NAME is UTF-16LE.  $VOLUME_INFORMATION is the 12-byte NTFS
       3.1 value: 8 reserved bytes, major/minor, flags. */
    static const uint8_t volume_name[] = {
        'G',0,'U',0,'P',0,'P',0,'Y',0
    };
    uint8_t info[12] = {0};
    info[8] = 3;
    info[9] = 1;
    put_u16(info + 10, 0); /* clean volume, no special flags */
    return ntfs_record_add_resident_attr(b, NTFS_ATTR_VOLUME_NAME,
                                         volume_name, sizeof volume_name) &&
           ntfs_record_add_resident_attr(b, NTFS_ATTR_VOLUME_INFORMATION,
                                         info, sizeof info);
}

static bool ntfs_record_add_empty_data(ntfs_record_builder_t *b)
{
    return ntfs_record_add_resident_attr(b, NTFS_ATTR_DATA, NULL, 0);
}

static bool ntfs_record_finish(ntfs_record_builder_t *b)
{
    ntfs_file_rec_t *fr = (ntfs_file_rec_t*)b->buf;
    if ((uint32_t)b->attr_cursor + 4u > b->rec_bytes) return false;
    put_u32(b->buf + b->attr_cursor, NTFS_ATTR_END);
    b->attr_cursor = align8_u16((uint16_t)(b->attr_cursor + 4u));
    fr->bytes_real = b->attr_cursor;
    fr->next_attr_id = b->next_attr_id;

    uint16_t *usa = (uint16_t*)(b->buf + b->usa_ofs);
    const uint16_t usn = (uint16_t)(0xA000u | ((b->rec_no + 1u) & 0x0FFFu));
    usa[0] = usn;
    for (uint16_t i=1; i<b->usa_count; ++i) {
        uint32_t tail = (uint32_t)i * b->sector_bytes - 2u;
        uint16_t original;
        memcpy(&original, b->buf + tail, sizeof original);
        usa[i] = original;
        put_u16(b->buf + tail, usn);
    }
    return true;
}

static bool write_metadata_record(FILE *f, const layout_t *L, uint64_t byte_off,
                                  uint32_t rec_no, const char *name,
                                  uint32_t file_attrs, bool directory,
                                  uint64_t parent_ref,
                                  bool add_data, uint64_t data_lcn, uint64_t data_clusters,
                                  uint64_t data_alloc, uint64_t data_size,
                                  const ntfs_index_seed_t *index_entries, size_t index_count)
{
    ntfs_record_builder_t b;
    if (!ntfs_record_init(&b, L, rec_no, directory)) return false;
    if (!ntfs_record_add_standard_information(&b, file_attrs)) return false;
    if (!ntfs_record_add_file_name(&b, parent_ref, name, file_attrs, directory)) return false;
    if (rec_no == 3 && !ntfs_record_add_volume_metadata(&b)) return false;
    if (rec_no == 8 && !ntfs_record_add_empty_data(&b)) return false;
    if (add_data && !ntfs_record_add_nonresident_data(&b, data_lcn, data_clusters,
                                                       data_alloc, data_size, data_size)) return false;
    if (index_entries && !ntfs_record_add_index_root(&b, L, index_entries, index_count)) return false;
    if (!ntfs_record_finish(&b)) return false;
    if (fseek(f, (long)byte_off, SEEK_SET) != 0) return false;
    return fwrite(b.buf, 1, sizeof b.buf, f) == sizeof b.buf;
}

int mkfs_ntfs_core(const mkfs_ntfs_opts_t *opt){
    if (!opt || !opt->image_path){ fprintf(stderr,"mkfs.ntfs(core): bad args\n"); return 2; }

    uint16_t bps = opt->bytes_per_sec ? opt->bytes_per_sec : 512;
    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096){
        fprintf(stderr,"mkfs.ntfs(core): bytes/sector must be 512/1024/2048/4096\n");
        return 2;
    }

    FILE *f = fopen(opt->image_path,"r+b");
    if (!f){ perror("open"); return 1; }

    uint64_t host_bytes = fsize(f);
    uint64_t base_bytes = (uint64_t)opt->lba_offset * (uint64_t)bps;
    uint64_t bytes_total = opt->size_bytes ? opt->size_bytes
                                           : (host_bytes > base_bytes ? host_bytes - base_bytes : 0);
    if (base_bytes > host_bytes || bytes_total > host_bytes - base_bytes) {
        fclose(f);
        fprintf(stderr,"mkfs.ntfs(core): requested volume exceeds backing image\n");
        return 3;
    }
    if (bytes_total < (uint64_t)bps * 100){
        fclose(f);
        fprintf(stderr,"mkfs.ntfs(core): image too small\n");
        return 3;
    }

    uint64_t totsec = bytes_total / bps;
    layout_t L;
    plan_layout(opt, totsec, &L);

    write_boot_sector(f, &L, opt);

    const uint32_t mft_seed_records = 16;
    const uint32_t mftmirr_records = 4;
    const uint64_t total_clusters = L.total_sectors / L.spc;
    const uint64_t mft_clusters = ((uint64_t)mft_seed_records * L.bytes_per_mftrec + L.bytes_per_cluster - 1u) / L.bytes_per_cluster;
    const uint64_t mirror_clusters = ((uint64_t)mftmirr_records * L.bytes_per_mftrec + L.bytes_per_cluster - 1u) / L.bytes_per_cluster;
    const uint64_t bitmap_bytes = (total_clusters + 7u) / 8u;
    const uint64_t bitmap_lcn = L.mftmirr_lcn + mirror_clusters;
    const uint64_t bitmap_clusters = (bitmap_bytes + L.bytes_per_cluster - 1u) / L.bytes_per_cluster;
    const uint64_t attrdef_lcn = bitmap_lcn + bitmap_clusters;
    const uint64_t attrdef_clusters = 1;
    const uint64_t logfile_lcn = attrdef_lcn + attrdef_clusters;
    const uint64_t logfile_clusters = 16; /* structural seed; restart pages come in hardening */
    const uint64_t boot_clusters = 2;     /* $Boot exposes the first 8192 bytes */
    if (total_clusters == 0 || L.mft_lcn + mft_clusters > L.mftmirr_lcn ||
        logfile_lcn + logfile_clusters >= total_clusters) {
        fclose(f);
        fprintf(stderr, "mkfs.ntfs(core): metadata layout does not fit volume\n");
        return 3;
    }
    uint64_t mft_byte_off = (L.lba_off * (uint64_t)L.bps) +
                            (L.mft_lcn * (uint64_t)L.bytes_per_cluster);
    uint64_t mftmirr_byte_off = (L.lba_off * (uint64_t)L.bps) +
                                (L.mftmirr_lcn * (uint64_t)L.bytes_per_cluster);
    zero_region(f, mft_byte_off, (uint64_t)mft_seed_records * L.bytes_per_mftrec);
    zero_region(f, mftmirr_byte_off, (uint64_t)mftmirr_records * L.bytes_per_mftrec);

    struct seed {
        uint32_t rec_no;
        const char *name;
        uint32_t attrs;
        bool directory;
        uint64_t parent_ref;
        bool add_data;
        uint64_t data_lcn, data_clusters, data_alloc, data_size;
    } seeds[] = {
        {0, "$MFT",     NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, true, L.mft_lcn, mft_clusters, mft_clusters * L.bytes_per_cluster, (uint64_t)mft_seed_records * L.bytes_per_mftrec},
        {1, "$MFTMirr", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, true, L.mftmirr_lcn, mirror_clusters, mirror_clusters * L.bytes_per_cluster, (uint64_t)mftmirr_records * L.bytes_per_mftrec},
        {2, "$LogFile", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, true, logfile_lcn, logfile_clusters, logfile_clusters * L.bytes_per_cluster, logfile_clusters * L.bytes_per_cluster},
        {3, "$Volume",  NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, false, 0,0,0,0},
        {4, "$AttrDef", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, true, attrdef_lcn, attrdef_clusters, attrdef_clusters * L.bytes_per_cluster, attrdef_clusters * L.bytes_per_cluster},
        {5, ".",        NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, true,  5, false, 0,0,0,0},
        {6, "$Bitmap",  NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, true, bitmap_lcn, bitmap_clusters, bitmap_clusters * L.bytes_per_cluster, bitmap_bytes},
        {7, "$Boot",    NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, true, 0, boot_clusters, boot_clusters * L.bytes_per_cluster, boot_clusters * L.bytes_per_cluster},
        {8, "$BadClus", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false, 5, false, 0,0,0,0},
    };

    /* Pass 4: the root directory now has a real resident $INDEX_ROOT.  Keep
       entries in filename-collation order for the ASCII system-file names. */
    const ntfs_index_seed_t root_index[] = {
        {4, "$AttrDef", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false},
        {6, "$Bitmap",  NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false},
        {2, "$LogFile", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false},
        {0, "$MFT",     NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false},
        {1, "$MFTMirr", NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false},
        {3, "$Volume",  NTFS_FILE_ATTR_HIDDEN | NTFS_FILE_ATTR_SYSTEM, false},
    };

    for (size_t i=0; i<sizeof seeds/sizeof seeds[0]; ++i) {
        const struct seed *s = &seeds[i];
        uint64_t off = mft_byte_off + (uint64_t)s->rec_no * L.bytes_per_mftrec;
        const ntfs_index_seed_t *idx = (s->rec_no == 5) ? root_index : NULL;
        size_t idx_count = (s->rec_no == 5) ? (sizeof root_index / sizeof root_index[0]) : 0;
        if (!write_metadata_record(f, &L, off, s->rec_no, s->name,
                                   s->attrs, s->directory, s->parent_ref,
                                   s->add_data, s->data_lcn, s->data_clusters,
                                   s->data_alloc, s->data_size, idx, idx_count)) {
            fclose(f);
            fprintf(stderr, "mkfs.ntfs(core): failed to build MFT record %u\n", s->rec_no);
            return 4;
        }
    }

    /* Pass 5: materialize the core system-file payloads.  $AttrDef gets a
       compact, independently checkable definition table; $LogFile is reserved
       as zeroed storage in this structural pass. */
    uint8_t attrdef[4096];
    memset(attrdef, 0, sizeof attrdef);
    struct attrdef_seed { const char *name; uint32_t type; uint32_t rule; } defs[] = {
        {"$STANDARD_INFORMATION", NTFS_ATTR_STANDARD_INFORMATION, 0x20},
        {"$FILE_NAME",            NTFS_ATTR_FILE_NAME,            0x30},
        {"$VOLUME_NAME",          NTFS_ATTR_VOLUME_NAME,          0x40},
        {"$VOLUME_INFORMATION",   NTFS_ATTR_VOLUME_INFORMATION,   0x50},
        {"$DATA",                 NTFS_ATTR_DATA,                 0x60},
        {"$INDEX_ROOT",           NTFS_ATTR_INDEX_ROOT,           0x90},
    };
    for (size_t d=0; d<sizeof defs/sizeof defs[0]; ++d) {
        uint8_t *e = attrdef + d * 160u;
        size_t n = strlen(defs[d].name);
        for (size_t j=0; j<n && j<63; ++j) { e[j*2] = (uint8_t)defs[d].name[j]; e[j*2+1] = 0; }
        put_u32(e + 128, defs[d].type);
        put_u32(e + 132, defs[d].rule);
        put_u32(e + 136, 0); /* collation rule */
        put_u32(e + 140, 0); /* flags */
        put_u64(e + 144, 0); /* minimum size */
        put_u64(e + 152, UINT64_MAX); /* maximum size */
    }
    uint64_t attrdef_byte_off = (L.lba_off * (uint64_t)L.bps) + attrdef_lcn * (uint64_t)L.bytes_per_cluster;
    if (fseek(f, (long)attrdef_byte_off, SEEK_SET) != 0 ||
        fwrite(attrdef, 1, sizeof attrdef, f) != sizeof attrdef) {
        fclose(f); fprintf(stderr, "mkfs.ntfs(core): failed to write $AttrDef data\n"); return 4;
    }
    zero_region(f, (L.lba_off * (uint64_t)L.bps) + logfile_lcn * (uint64_t)L.bytes_per_cluster,
                logfile_clusters * (uint64_t)L.bytes_per_cluster);

    /* Pass 3: materialize $Bitmap and account for every cluster consumed by
       the current formatter metadata.  Keep the bitmap data independent of
       the FILE record that describes it. */
    uint8_t *bitmap = (uint8_t*)calloc(1, (size_t)(bitmap_clusters * L.bytes_per_cluster));
    if (!bitmap) { fclose(f); return 4; }
    #define MARK_CLUSTER(c) do { uint64_t _c=(c); bitmap[_c >> 3] |= (uint8_t)(1u << (_c & 7u)); } while (0)
    for (uint64_t c=0; c<boot_clusters; ++c) MARK_CLUSTER(c); /* $Boot */
    for (uint64_t c=0; c<mft_clusters; ++c) MARK_CLUSTER(L.mft_lcn + c);
    for (uint64_t c=0; c<mirror_clusters; ++c) MARK_CLUSTER(L.mftmirr_lcn + c);
    for (uint64_t c=0; c<bitmap_clusters; ++c) MARK_CLUSTER(bitmap_lcn + c);
    for (uint64_t c=0; c<attrdef_clusters; ++c) MARK_CLUSTER(attrdef_lcn + c);
    for (uint64_t c=0; c<logfile_clusters; ++c) MARK_CLUSTER(logfile_lcn + c);
    MARK_CLUSTER(total_clusters - 1u); /* cluster containing backup boot sector */
    #undef MARK_CLUSTER
    uint64_t bitmap_byte_off = (L.lba_off * (uint64_t)L.bps) + bitmap_lcn * (uint64_t)L.bytes_per_cluster;
    if (fseek(f, (long)bitmap_byte_off, SEEK_SET) != 0 ||
        fwrite(bitmap, 1, (size_t)(bitmap_clusters * L.bytes_per_cluster), f) !=
            (size_t)(bitmap_clusters * L.bytes_per_cluster)) {
        free(bitmap); fclose(f);
        fprintf(stderr, "mkfs.ntfs(core): failed to write $Bitmap data\n");
        return 4;
    }
    free(bitmap);

    /* NTFS mirrors the first four MFT records.  Copy exact on-disk records so
       the checker can prove both the USA substitution and mirrored bytes. */
    uint8_t mirror_buf[1024];
    for (uint32_t rec=0; rec<mftmirr_records; ++rec) {
        if (fseek(f, (long)(mft_byte_off + (uint64_t)rec * L.bytes_per_mftrec), SEEK_SET) != 0 ||
            fread(mirror_buf, 1, sizeof mirror_buf, f) != sizeof mirror_buf ||
            fseek(f, (long)(mftmirr_byte_off + (uint64_t)rec * L.bytes_per_mftrec), SEEK_SET) != 0 ||
            fwrite(mirror_buf, 1, sizeof mirror_buf, f) != sizeof mirror_buf) {
            fclose(f);
            fprintf(stderr, "mkfs.ntfs(core): failed to seed $MFTMirr record %u\n", rec);
            return 4;
        }
    }


    fclose(f);

    if (opt->verbose){
        printf("mkfs.ntfs(core): %s\n", opt->image_path);
        printf("  bps=%u spc=%u cluster=%u B  total_sectors=%llu\n",
               L.bps, L.spc, L.bytes_per_cluster, (unsigned long long)L.total_sectors);
        printf("  mft_lcn=%llu (byte_off=%llu), mftmirr_lcn=%llu\n",
               (unsigned long long)L.mft_lcn, (unsigned long long)mft_byte_off,
               (unsigned long long)L.mftmirr_lcn);
        printf("  seeded MFT metadata records: 0..8 with core system-file semantics\n");
        printf("  $MFT/$MFTMirr nonresident $DATA runlists: clusters %llu/%llu\n",
               (unsigned long long)mft_clusters, (unsigned long long)mirror_clusters);
        printf("  root $INDEX_ROOT: 6 resident metadata entries (Pass 4 resident set)\n");
        printf("  $Volume: name=GUPPY version=3.1 flags=0\n");
        printf("  $AttrDef: lcn=%llu clusters=%llu\n",
               (unsigned long long)attrdef_lcn, (unsigned long long)attrdef_clusters);
        printf("  $LogFile: lcn=%llu clusters=%llu (structural zeroed seed)\n",
               (unsigned long long)logfile_lcn, (unsigned long long)logfile_clusters);
        printf("  $Boot: lcn=0 clusters=%llu; $BadClus: empty $DATA seed\n",
               (unsigned long long)boot_clusters);
        printf("  $Bitmap: lcn=%llu clusters=%llu data_bytes=%llu\n",
               (unsigned long long)bitmap_lcn, (unsigned long long)bitmap_clusters,
               (unsigned long long)bitmap_bytes);
        printf("  mirrored MFT records: 0..3\n");
    }

    return 0;
}
