#include <stdio.h>
#include <string.h>

#include "gu_path.h"
#include "vfs.h"

int gu_path_parse(const char *text, gu_path_t *out)
{
    if (!text || !out) return -1;

    if (strncmp(text, "local:", 6) == 0) {
        out->space = GU_PATH_LOCAL;
        out->path = text + 6;
        return out->path[0] ? 0 : -1;
    }

    out->space = GU_PATH_VFS;
    out->path = text;
    return out->path[0] ? 0 : -1;
}

FILE *gu_path_open_seekable_read(const char *text, gu_path_t *parsed_out)
{
    gu_path_t p;

    if (gu_path_parse(text, &p) != 0)
        return NULL;
    if (parsed_out)
        *parsed_out = p;

    if (p.space == GU_PATH_LOCAL)
        return fopen(p.path, "rb");

    {
        struct file *in = NULL;
        FILE *tmp = NULL;
        unsigned char buf[64 * 1024];

        if (vfs_open(p.path, VFS_O_RDONLY, 0, &in) != 0 || !in)
            return NULL;

        tmp = tmpfile();
        if (!tmp) {
            (void)vfs_close(in);
            return NULL;
        }

        for (;;) {
            ssize_t n = vfs_read(in, buf, sizeof buf);
            if (n < 0) {
                (void)vfs_close(in);
                fclose(tmp);
                return NULL;
            }
            if (n == 0)
                break;

            if (fwrite(buf, 1, (size_t)n, tmp) != (size_t)n) {
                (void)vfs_close(in);
                fclose(tmp);
                return NULL;
            }
        }

        if (vfs_close(in) != 0 || fflush(tmp) != 0 ||
            fseek(tmp, 0, SEEK_SET) != 0) {
            fclose(tmp);
            return NULL;
        }
        return tmp;
    }
}
