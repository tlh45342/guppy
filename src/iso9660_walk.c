// src/iso9660_walk.c

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "iso9660.h"  // public API
#include "vblk.h"     // vblk_t, vblk_read_blocks
#include "debug.h"    // DBG(...)

// --- drop-in replacement: src/iso9660.c ---
static inline uint32_t u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void iso_normalize_name(const char *raw, int raw_len, char *out, size_t out_cap) {
    // Strip version suffix (";1") and lowercase A..Z
    size_t o = 0;
    for (int i = 0; i < raw_len && o + 1 < out_cap; i++) {
        char c = raw[i];
        if (c == ';') break;                   // stop at version delimiter
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[o++] = c;
    }
    out[o] = '\0';
}

static int names_equal_ci(const char *a, const char *b) {
    // case-insensitive compare (ASCII)
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* Return the byte offset at which the System Use Area starts. */
static size_t iso_sua_offset(const uint8_t *rec) {
    uint8_t id_len = rec[32];
    size_t off = 33u + (size_t)id_len;
    /* ISO9660 adds one padding byte when the identifier length is even. */
    if ((id_len & 1u) == 0) off++;
    return off;
}

/* RRIP NM flags (RRIP 1.12). */
enum {
    RR_NM_CONTINUE = 0x01,
    RR_NM_CURRENT  = 0x02,
    RR_NM_PARENT   = 0x04,
};


#define RR_CE_MAX_DEPTH 8u
#define RR_CE_MAX_BYTES (1024u * 1024u)

typedef bool (*rr_su_visit_fn)(const uint8_t *su, size_t slen, void *ctx);

static bool rr_read_bytes(const iso9660_t *iso, uint32_t block,
                          uint32_t offset, uint32_t length, uint8_t *dst)
{
    if (!iso || !dst) return false;
    uint8_t sec[ISO_SECTOR_SIZE];

    uint64_t abs = (uint64_t)block * ISO_SECTOR_SIZE + offset;
    uint32_t left = length;
    while (left) {
        uint32_t lba = (uint32_t)(abs / ISO_SECTOR_SIZE);
        uint32_t in  = (uint32_t)(abs % ISO_SECTOR_SIZE);
        uint32_t take = ISO_SECTOR_SIZE - in;
        if (take > left) take = left;
        if (!iso_read_sector(iso, lba, sec)) return false;
        memcpy(dst, sec + in, take);
        dst += take;
        abs += take;
        left -= take;
    }
    return true;
}

static bool rr_walk_su_area(const iso9660_t *iso,
                            const uint8_t *area, size_t area_len,
                            unsigned depth, size_t *budget,
                            rr_su_visit_fn visit, void *ctx)
{
    if (!iso || !area || !budget || !visit || depth > RR_CE_MAX_DEPTH)
        return false;

    size_t pos = 0;
    while (pos + 4u <= area_len) {
        const uint8_t *su = area + pos;
        uint8_t slen = su[2];
        if (slen < 4u || pos + slen > area_len) break;

        if (!visit(su, slen, ctx)) return true; /* caller found enough */

        if (su[0] == 'C' && su[1] == 'E' && su[3] == 1u && slen >= 28u) {
            uint32_t block  = u32le(su + 4);
            uint32_t offset = u32le(su + 12);
            uint32_t length = u32le(su + 20);

            if (length && length <= *budget && depth < RR_CE_MAX_DEPTH) {
                uint8_t *buf = (uint8_t *)malloc(length);
                if (!buf) return false;
                if (!rr_read_bytes(iso, block, offset, length, buf)) {
                    free(buf);
                    return false;
                }
                *budget -= length;
                bool ok = rr_walk_su_area(iso, buf, length, depth + 1u,
                                          budget, visit, ctx);
                free(buf);
                if (!ok) return false;
            }
        }

        if (su[0] == 'S' && su[1] == 'T') break;
        pos += slen;
    }
    return true;
}

static bool rr_walk_record_su(const iso9660_t *iso, const uint8_t *rec,
                              rr_su_visit_fn visit, void *ctx)
{
    if (!iso || !iso->susp_enabled || !rec || !visit) return false;
    uint8_t rec_len = rec[0];
    if (rec_len < 34u) return false;

    size_t pos = iso_sua_offset(rec);
    if (pos > rec_len) return false;
    pos += iso->susp_skip;
    if (pos > rec_len) return false;

    size_t budget = RR_CE_MAX_BYTES;
    return rr_walk_su_area(iso, rec + pos, rec_len - pos, 0u,
                           &budget, visit, ctx);
}

typedef struct {
    char *out;
    size_t cap;
    size_t used;
    bool found;
} rr_nm_ctx_t;

static bool rr_nm_visit(const uint8_t *su, size_t slen, void *vctx)
{
    rr_nm_ctx_t *ctx = (rr_nm_ctx_t *)vctx;
    if (su[0] != 'N' || su[1] != 'M' || su[3] != 1u || slen < 5u)
        return true;

    uint8_t flags = su[4];
    if (flags & 0x02u) { /* current */
        if (ctx->cap) { ctx->out[0]='.'; if (ctx->cap>1) ctx->out[1]='\0'; }
        ctx->used=1; ctx->found=true; return false;
    }
    if (flags & 0x04u) { /* parent */
        if (ctx->cap) {
            ctx->out[0]='.';
            if (ctx->cap>1) ctx->out[1]='.';
            if (ctx->cap>2) ctx->out[2]='\0';
        }
        ctx->used=2; ctx->found=true; return false;
    }

    size_t n = slen - 5u;
    if (ctx->cap && ctx->used < ctx->cap - 1u) {
        size_t room = ctx->cap - 1u - ctx->used;
        if (n > room) n = room;
        memcpy(ctx->out + ctx->used, su + 5, n);
        ctx->used += n;
        ctx->out[ctx->used] = '\0';
    }
    ctx->found = true;

    /* NM CONTINUE means another NM fragment follows, possibly through CE. */
    return (flags & 0x01u) != 0;
}

bool iso_dir_record_name_ce(const iso9660_t *iso, const uint8_t *rec,
                            char *out, size_t out_cap, bool *used_rr_nm)
{
    if (used_rr_nm) *used_rr_nm = false;
    if (!rec || !out || out_cap == 0u) return false;

    rr_nm_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.out = out;
    ctx.cap = out_cap;
    out[0] = '\0';

    if (iso && iso->susp_enabled)
        (void)rr_walk_record_su(iso, rec, rr_nm_visit, &ctx);

    if (ctx.found) {
        if (used_rr_nm) *used_rr_nm = true;
        return true;
    }
    return iso_dir_record_name(iso, rec, out, out_cap, used_rr_nm);
}

typedef struct { iso_rr_px_t *px; } rr_px_ctx_t;
static bool rr_px_visit(const uint8_t *su, size_t slen, void *vctx)
{
    rr_px_ctx_t *ctx=(rr_px_ctx_t *)vctx;
    if (su[0]=='P' && su[1]=='X' && su[3]==1u && slen>=36u) {
        ctx->px->present=true;
        ctx->px->mode=u32le(su+4);
        ctx->px->nlink=u32le(su+12);
        ctx->px->uid=u32le(su+20);
        ctx->px->gid=u32le(su+28);
        return false;
    }
    return true;
}
bool iso_dir_record_px_ce(const iso9660_t *iso, const uint8_t *rec,
                          iso_rr_px_t *out_px)
{
    if (out_px) memset(out_px,0,sizeof *out_px);
    if (!out_px) return false;
    rr_px_ctx_t ctx={out_px};
    (void)rr_walk_record_su(iso,rec,rr_px_visit,&ctx);
    return out_px->present;
}

typedef struct { iso_rr_tf_t *tf; } rr_tf_ctx_t;
static bool rr_tf_visit(const uint8_t *su, size_t slen, void *vctx)
{
    rr_tf_ctx_t *ctx=(rr_tf_ctx_t *)vctx;
    /* Reuse the existing TF decoder logic by copying one TF SU into a
       synthetic directory record whose SUA begins at byte 34. */
    if (su[0]=='T' && su[1]=='F' && su[3]==1u && slen>=5u) {
        uint8_t rec[255];
        if (34u+slen > sizeof rec) return true;
        memset(rec,0,sizeof rec);
        rec[0]=(uint8_t)(34u+slen);
        rec[32]=1; rec[33]='X';
        memcpy(rec+34,su,slen);

        iso9660_t tmp;
        memset(&tmp,0,sizeof tmp);
        tmp.susp_enabled=true;
        tmp.susp_skip=0;
        (void)iso_dir_record_tf(&tmp,rec,ctx->tf);
        return false;
    }
    return true;
}
bool iso_dir_record_tf_ce(const iso9660_t *iso, const uint8_t *rec,
                          iso_rr_tf_t *out_tf)
{
    if (out_tf) memset(out_tf,0,sizeof *out_tf);
    if (!out_tf) return false;
    rr_tf_ctx_t ctx={out_tf};
    (void)rr_walk_record_su(iso,rec,rr_tf_visit,&ctx);
    return out_tf->present;
}


typedef struct {
    char *out;
    size_t cap;
    size_t used;
    bool found;
    bool need_sep;
} rr_sl_ctx_t;

static bool rr_sl_append(rr_sl_ctx_t *ctx, const char *s, size_t n)
{
    if (!ctx || !ctx->out || ctx->cap == 0u) return false;

    if (ctx->need_sep && ctx->used > 0u && ctx->out[ctx->used - 1u] != '/') {
        if (ctx->used + 1u >= ctx->cap) return false;
        ctx->out[ctx->used++] = '/';
    }
    if (ctx->used + n >= ctx->cap) return false;
    if (n) memcpy(ctx->out + ctx->used, s, n);
    ctx->used += n;
    ctx->out[ctx->used] = '\0';
    ctx->need_sep = true;
    return true;
}

static bool rr_sl_visit(const uint8_t *su, size_t slen, void *vctx)
{
    rr_sl_ctx_t *ctx=(rr_sl_ctx_t *)vctx;
    if (su[0]!='S' || su[1]!='L' || su[3]!=1u || slen<5u)
        return true;

    ctx->found=true;
    uint8_t entry_flags=su[4];
    size_t pos=5u;

    while (pos+2u<=slen) {
        uint8_t cflags=su[pos];
        uint8_t clen=su[pos+1u];
        pos+=2u;
        if (pos+clen>slen) break;

        /* RRIP SL component flags:
           0x01 CONTINUE, 0x02 CURRENT, 0x04 PARENT, 0x08 ROOT,
           0x10 VOLROOT, 0x20 HOST. */
        if (cflags & 0x08u) {
            if (ctx->cap < 2u) return false;
            ctx->out[0]='/'; ctx->out[1]='\0';
            ctx->used=1u;
            ctx->need_sep=false;
        } else if (cflags & 0x04u) {
            if (!rr_sl_append(ctx,"..",2u)) return false;
        } else if (cflags & 0x02u) {
            if (!rr_sl_append(ctx,".",1u)) return false;
        } else if (cflags & 0x10u) {
            /* Volume root is represented as an absolute root. */
            if (ctx->cap < 2u) return false;
            ctx->out[0]='/'; ctx->out[1]='\0';
            ctx->used=1u;
            ctx->need_sep=false;
        } else if (cflags & 0x20u) {
            if (!rr_sl_append(ctx,"HOST",4u)) return false;
        } else {
            if (!rr_sl_append(ctx,(const char *)(su+pos),clen)) return false;
        }

        /* Component CONTINUE means the next component fragment continues the
           same pathname component, so suppress the separator once. */
        if (cflags & 0x01u) ctx->need_sep=false;
        pos+=clen;
    }

    /* SL entry CONTINUE means another SL entry follows, possibly in CE. */
    return (entry_flags & 0x01u) != 0;
}

bool iso_dir_record_sl_ce(const iso9660_t *iso, const uint8_t *rec,
                          char *out, size_t out_cap)
{
    if (!out || out_cap==0u) return false;
    out[0]='\0';

    rr_sl_ctx_t ctx;
    memset(&ctx,0,sizeof ctx);
    ctx.out=out;
    ctx.cap=out_cap;

    (void)rr_walk_record_su(iso,rec,rr_sl_visit,&ctx);
    return ctx.found && out[0]!='\0';
}

bool iso_dir_record_name(const iso9660_t *iso, const uint8_t *rec,
                         char *out, size_t out_cap, bool *used_rr_nm)
{
    if (used_rr_nm) *used_rr_nm = false;
    if (!rec || !out || out_cap == 0) return false;
    out[0] = '\0';

    const uint8_t rec_len = rec[0];
    if (rec_len < 34) return false;

    const uint8_t id_len = rec[32];
    if ((size_t)33u + id_len > rec_len) return false;
    const uint8_t *id = rec + 33;

    /* ISO special identifiers always remain dot/dotdot. */
    if (id_len == 1 && id[0] == 0) {
        if (out_cap < 2) return false;
        strcpy(out, ".");
        return true;
    }
    if (id_len == 1 && id[0] == 1) {
        if (out_cap < 3) return false;
        strcpy(out, "..");
        return true;
    }

    if (iso && iso->susp_enabled) {
        size_t pos = iso_sua_offset(rec);
        if (pos <= rec_len) {
            pos += iso->susp_skip;
            if (pos <= rec_len) {
                size_t oi = 0;
                bool saw_nm = false;
                while (pos + 4u <= rec_len) {
                    const uint8_t *su = rec + pos;
                    uint8_t slen = su[2];
                    if (slen < 4u || pos + slen > rec_len) break;

                    if (su[0] == 'N' && su[1] == 'M' && slen >= 5u) {
                        uint8_t flags = su[4];
                        if (flags & RR_NM_CURRENT) {
                            if (out_cap < 2) return false;
                            strcpy(out, ".");
                            if (used_rr_nm) *used_rr_nm = true;
                            return true;
                        }
                        if (flags & RR_NM_PARENT) {
                            if (out_cap < 3) return false;
                            strcpy(out, "..");
                            if (used_rr_nm) *used_rr_nm = true;
                            return true;
                        }

                        size_t part = (size_t)slen - 5u;
                        if (oi + part >= out_cap) part = out_cap - 1u - oi;
                        if (part) memcpy(out + oi, su + 5, part);
                        oi += part;
                        out[oi] = '\0';
                        saw_nm = true;

                        /* Multiple NM entries can concatenate a long name. */
                        if ((flags & RR_NM_CONTINUE) == 0) break;
                    }
                    pos += slen;
                }
                if (saw_nm) {
                    if (used_rr_nm) *used_rr_nm = true;
                    return true;
                }
            }
        }
    }

    /* Primary ISO9660 fallback: strip ;version, preserve the historical
       lowercase lookup form used by iso_walk_component(). */
    iso_normalize_name((const char*)id, (int)id_len, out, out_cap);
    return out[0] != '\0';
}


bool iso_dir_record_px(const iso9660_t *iso, const uint8_t *rec,
                       iso_rr_px_t *out_px)
{
    if (out_px) memset(out_px, 0, sizeof *out_px);
    if (!iso || !iso->susp_enabled || !rec || !out_px) return false;

    const uint8_t rec_len = rec[0];
    if (rec_len < 34u) return false;

    size_t pos = iso_sua_offset(rec);
    if (pos > rec_len) return false;
    pos += iso->susp_skip;
    if (pos > rec_len) return false;

    while (pos + 4u <= rec_len) {
        const uint8_t *su = rec + pos;
        uint8_t slen = su[2];
        if (slen < 4u || pos + slen > rec_len) break;

        /* PX: header(4) + mode(8) + nlink(8) + uid(8) + gid(8). */
        if (su[0] == 'P' && su[1] == 'X' && su[3] == 1u && slen >= 36u) {
            out_px->present = true;
            out_px->mode  = u32le(su + 4);
            out_px->nlink = u32le(su + 12);
            out_px->uid   = u32le(su + 20);
            out_px->gid   = u32le(su + 28);
            return true;
        }
        pos += slen;
    }
    return false;
}


static int rr_dec2(const uint8_t *p)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    return (p[0] - '0') * 10 + (p[1] - '0');
}

static int rr_dec4(const uint8_t *p)
{
    int a = rr_dec2(p), b = rr_dec2(p + 2);
    if (a < 0 || b < 0) return -1;
    return a * 100 + b;
}

static int64_t rr_days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u
                       + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static time_t rr_utc_time(int year, int mon, int day, int hour, int min, int sec)
{
    int64_t days = rr_days_from_civil(year, (unsigned)mon, (unsigned)day);
    int64_t total = days * 86400 + hour * 3600 + min * 60 + sec;
    return (time_t)total;
}

static bool rr_tf_time7(const uint8_t *p, time_t *out)
{
    struct tm tmv;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = (int)p[0];
    tmv.tm_mon  = (int)p[1] - 1;
    tmv.tm_mday = (int)p[2];
    tmv.tm_hour = (int)p[3];
    tmv.tm_min  = (int)p[4];
    tmv.tm_sec  = (int)p[5];

    time_t t = rr_utc_time(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                         tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    /* Byte 6 is signed 15-minute offset from GMT. */
    t -= (time_t)((int8_t)p[6]) * 15 * 60;
    *out = t;
    return true;
}

static bool rr_tf_time17(const uint8_t *p, time_t *out)
{
    int year = rr_dec4(p);
    int mon  = rr_dec2(p + 4);
    int day  = rr_dec2(p + 6);
    int hour = rr_dec2(p + 8);
    int min  = rr_dec2(p + 10);
    int sec  = rr_dec2(p + 12);
    if (year < 0 || mon < 1 || mon > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60)
        return false;

    struct tm tmv;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = year - 1900;
    tmv.tm_mon  = mon - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min  = min;
    tmv.tm_sec  = sec;

    time_t t = rr_utc_time(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                         tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    /* Byte 16 is signed 15-minute offset from GMT. */
    t -= (time_t)((int8_t)p[16]) * 15 * 60;
    *out = t;
    return true;
}

bool iso_dir_record_tf(const iso9660_t *iso, const uint8_t *rec,
                       iso_rr_tf_t *out_tf)
{
    if (out_tf) memset(out_tf, 0, sizeof *out_tf);
    if (!iso || !iso->susp_enabled || !rec || !out_tf) return false;

    const uint8_t rec_len = rec[0];
    if (rec_len < 34u) return false;

    size_t pos = iso_sua_offset(rec);
    if (pos > rec_len) return false;
    pos += iso->susp_skip;
    if (pos > rec_len) return false;

    while (pos + 5u <= rec_len) {
        const uint8_t *su = rec + pos;
        uint8_t slen = su[2];
        if (slen < 5u || pos + slen > rec_len) break;

        if (su[0] == 'T' && su[1] == 'F' && su[3] == 1u) {
            const uint8_t flags = su[4];
            const bool long_form = (flags & 0x80u) != 0;
            const size_t tsz = long_form ? 17u : 7u;
            size_t off = 5u;
            time_t t;

            /* TF order: creation, modify, access, attributes,
               backup, expiration, effective. */
            if (flags & 0x01u) off += tsz; /* creation */

            if (flags & 0x02u) {
                if (off + tsz > slen) return false;
                if ((long_form ? rr_tf_time17(su + off, &t)
                               : rr_tf_time7(su + off, &t))) {
                    out_tf->mtime = t;
                    out_tf->has_mtime = true;
                }
                off += tsz;
            }

            if (flags & 0x04u) {
                if (off + tsz > slen) return false;
                if ((long_form ? rr_tf_time17(su + off, &t)
                               : rr_tf_time7(su + off, &t))) {
                    out_tf->atime = t;
                    out_tf->has_atime = true;
                }
                off += tsz;
            }

            if (flags & 0x08u) {
                if (off + tsz > slen) return false;
                if ((long_form ? rr_tf_time17(su + off, &t)
                               : rr_tf_time7(su + off, &t))) {
                    out_tf->ctime = t;
                    out_tf->has_ctime = true;
                }
            }

            out_tf->present = out_tf->has_mtime ||
                              out_tf->has_atime ||
                              out_tf->has_ctime;
            return out_tf->present;
        }

        pos += slen;
    }
    return false;
}

static time_t iso_recdate_to_time(const uint8_t rec[7]) {
    struct tm t;
    t.tm_year = rec[0];          // years since 1900
    t.tm_mon  = (rec[1] ? rec[1] - 1 : 0); // 1..12 -> 0..11
    t.tm_mday = rec[2];
    t.tm_hour = rec[3];
    t.tm_min  = rec[4];
    t.tm_sec  = rec[5];
    t.tm_isdst = -1;

    /* Treat fields as local time; ignore tz byte for now (rec[6]).
       We can refine with a true UTC conversion later if needed. */
    return mktime(&t);
}

/**
 * Scan a single ISO9660 directory (at dir_lba, length dir_size bytes) for one component name.
 * If found, outputs the child's extent LBA/size/flags and returns 1.
 * If not found, returns 0. On read/parse error, returns -1.
 */
int iso_walk_component(const iso9660_t *iso,
                          uint32_t dir_lba,
                          uint32_t dir_size,
                          const char *want,
                          uint32_t *out_lba,
                          uint32_t *out_size,
                          uint8_t *out_flags,
                          time_t *out_mtime,
                          iso_rr_px_t *out_px,
                          iso_rr_tf_t *out_tf,
                          char *out_sl, size_t out_sl_cap)
{
    if (out_sl && out_sl_cap) out_sl[0] = '\0';
    if (out_tf) memset(out_tf, 0, sizeof *out_tf);
    if (out_px) memset(out_px, 0, sizeof *out_px);
    if (!iso || !iso->dev || !want) return -1;

    const uint32_t bs = 2048u;

    uint8_t sec[2048u];
    uint32_t bytes_left = dir_size;
    uint32_t cur_lba = dir_lba;
    uint32_t off_in_dir = 0;

    while (bytes_left > 0) {
        if (!iso_read_sector(iso, cur_lba, sec)) {
            DBG("iso: read error dir_lba=%u (cur_lba=%u)", dir_lba, cur_lba);
            return -1;
        }

        uint32_t in = 0;
        while (in < bs && bytes_left > 0) {
            uint8_t rec_len = sec[in + 0];
            if (rec_len == 0) break; // end of records in this sector

            if (in + rec_len > bs) {
                DBG("iso: truncated dirent (rec_len=%u beyond sector)", (unsigned)rec_len);
                return -1;
            }

            const uint8_t *rec = &sec[in];

            // Parse fields by spec offsets (LE copy)
            uint32_t child_lba  = u32le(&rec[2]);   // extent location (LBA)
            uint32_t child_size = u32le(&rec[10]);  // data length (bytes)
            uint8_t  flags      = rec[25];          // file flags
            uint8_t  name_len   = rec[32];
            const char *name_ptr = (const char *)&rec[33];

            if (33u + name_len > in + rec_len) {
                DBG("iso: bad name_len=%u (rec_len=%u)", (unsigned)name_len, (unsigned)rec_len);
                return -1;
            }

            // Skip special names 0x00="." and 0x01=".."
            int is_dot    = (name_len == 1 && (unsigned char)name_ptr[0] == 0x00);
            int is_dotdot = (name_len == 1 && (unsigned char)name_ptr[0] == 0x01);

            char clean[256];
            bool used_rr_nm = false;
            if (!iso_dir_record_name(iso, rec, clean, sizeof clean, &used_rr_nm)) {
                DBG("iso: unable to decode directory-record name");
                return -1;
            }

            // DEBUG: show what we parsed
            DBG("iso: dirent raw='%.*s' clean='%s' flags=0x%02X (%s) lba=%u size=%u rec_len=%u off=%u",
                (int)name_len, name_ptr,
                clean,
                (unsigned)flags, (flags & 0x02) ? "DIR" : "FILE",
                (unsigned)child_lba, (unsigned)child_size, (unsigned)rec_len, (unsigned)off_in_dir);

            int name_match = used_rr_nm ? (strcmp(clean, want) == 0)
                                        : names_equal_ci(clean, want);
            if (!is_dot && !is_dotdot && name_match) {
                if (out_lba)   *out_lba   = child_lba;
                if (out_size)  *out_size  = child_size;
                if (out_flags) *out_flags = flags;
                if (out_mtime) *out_mtime = iso_recdate_to_time(rec + 18);
                if (out_px) (void)iso_dir_record_px_ce(iso, rec, out_px);
                if (out_tf) (void)iso_dir_record_tf_ce(iso, rec, out_tf);
                if (out_sl && out_sl_cap)
                    (void)iso_dir_record_sl_ce(iso, rec, out_sl, out_sl_cap);
                return 1; // found
            }

            in += rec_len;
            off_in_dir += rec_len;
            if (bytes_left >= rec_len) bytes_left -= rec_len; else bytes_left = 0;
        }

        cur_lba++;
    }

    return 0; // not found
}