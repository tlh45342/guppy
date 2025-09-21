#include <stdlib.h>
#include <string.h>
#include "gu_dirent.h"
#include "vfs.h"

/* Packed directory record, defined in vfs.h */
typedef struct vfs_dirent64 vfs_dirent64_t;

/* Opaque DIR implementation */
struct DIR {
    struct file *f;          /* open directory handle */
    size_t       off;        /* current parse offset in buf */
    size_t       len;        /* valid bytes in buf */
    unsigned char buf[4096]; /* batch buffer for getdents64 */
    struct dirent ent;       /* last returned entry */
};

static inline ssize_t call_getdents64(struct file *f, void *buf, size_t bytes) {
    if (!f || !f->f_op || !f->f_op->getdents64) return -1;
    return f->f_op->getdents64(f, buf, bytes);
}

DIR* vfs_opendir(const char *path) {
    if (!path) return NULL;
    struct file *df = NULL;
    if (vfs_open(path, VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &df) != 0 || !df) return NULL;

    DIR *d = (DIR*)calloc(1, sizeof(DIR));
    if (!d) { vfs_close(df); return NULL; }
    d->f = df;
    d->off = 0;
    d->len = 0;
    d->ent.d_ino = 0;
    d->ent.d_type = VFS_DT_UNKNOWN;
    d->ent.d_name[0] = '\0';
    return d;
}

static int refill(DIR *d) {
    d->off = 0;
    d->len = 0;
    ssize_t n = call_getdents64(d->f, d->buf, sizeof d->buf);
    if (n < 0)  return -1;   /* error */
    if (n == 0) return 0;    /* end of directory */
    d->len = (size_t)n;
    return 1;
}

struct dirent* vfs_readdir(DIR *d) {
    if (!d || !d->f) return NULL;

    for (;;) {
        if (d->off >= d->len) {
            int r = refill(d);
            if (r <= 0) return NULL; /* end or error => NULL (POSIX readdir) */
        }

        /* Safe parse of one variable-length record */
        if (d->len - d->off < sizeof(vfs_dirent64_t)) return NULL; /* malformed */
        vfs_dirent64_t *r = (vfs_dirent64_t*)(d->buf + d->off);
        size_t reclen = r->d_reclen;
        if (reclen < sizeof(vfs_dirent64_t) || d->off + reclen > d->len) return NULL;

        /* Copy out to fixed struct dirent */
        d->ent.d_ino  = r->d_ino;
        d->ent.d_type = r->d_type;

        /* Compute max available for name inside this record and copy */
		size_t max_in_rec = reclen - offsetof(vfs_dirent64_t, d_name);
		/* Find NUL within record bounds */
		const void *nul = memchr(r->d_name, '\0', max_in_rec);
		size_t nlen = nul ? (size_t)((const char*)nul - r->d_name) : max_in_rec;
		if (nlen > GU_DIRENT_NAME_MAX) nlen = GU_DIRENT_NAME_MAX;
		memcpy(d->ent.d_name, r->d_name, nlen);
		d->ent.d_name[nlen] = '\0';

        d->off += reclen;

        /* Return every entry (including "." and ".."); caller can filter */
        return &d->ent;
    }
}

int vfs_closedir(DIR *d) {
    if (!d) return 0;
    if (d->f) vfs_close(d->f);
    free(d);
    return 0;
}

void vfs_rewinddir(DIR *d) {
    if (!d || !d->f) return;
    /* Reset our buffer */
    d->off = 0;
    d->len = 0;
    /* If driver supports llseek, reset stream position */
    if (d->f->f_op && d->f->f_op->llseek) {
        uint64_t newpos = 0;
        d->f->f_op->llseek(d->f, 0, VFS_SEEK_SET, &newpos);
    }
}
