#include "vdisk_internal.h"

static int raw_probe(FILE *fp) { (void)fp; return 1; }

static int raw_open(vdisk_t *d) {
    uint64_t n = 0;
    if (vdisk_host_get_size(d->fp, &n) != 0) return -1;
    d->size_bytes = n;
    d->host_size_bytes = n;
    d->data_offset = 0;
    return 0;
}

static int raw_create(vdisk_t *d, uint64_t size) {
    if (!size) return -1;
    if (vdisk_host_seek(d->fp, size - 1) != 0) return -1;
    if (fputc(0, d->fp) == EOF) return -1;
    d->size_bytes = size;
    d->host_size_bytes = size;
    d->data_offset = 0;
    return fflush(d->fp) == 0 ? 0 : -1;
}

static int bounds(vdisk_t *d, uint64_t off, size_t len) {
    return off <= d->size_bytes && (uint64_t)len <= d->size_bytes - off ? 0 : -1;
}
static int raw_read(vdisk_t *d,uint64_t o,void*b,size_t n) {
    return bounds(d,o,n) ? -1 : vdisk_host_read(d->fp,o,b,n);
}
static int raw_write(vdisk_t *d,uint64_t o,const void*b,size_t n) {
    return (!d->writable || bounds(d,o,n)) ? -1 : vdisk_host_write(d->fp,o,b,n);
}
static int raw_flush(vdisk_t*d){ return fflush(d->fp)==0?0:-1; }
static void raw_close(vdisk_t*d){ (void)d; }

const vdisk_driver_t vdisk_raw_driver = {
    VDISK_FMT_RAW,"raw",raw_probe,raw_open,raw_create,
    raw_read,raw_write,raw_flush,raw_close
};
