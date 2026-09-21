#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "logfile.h"
#include "app_printf.h"

static FILE *s_logfile;
static char s_log_path[512];

static void error_vprintf(const char *fmt, va_list ap)
{
    va_list log_ap;

    va_copy(log_ap, ap);
    vfprintf(stderr, fmt, ap);
    fflush(stderr);
    guppy_log_vwrite(fmt, log_ap);
    va_end(log_ap);
}

int guppy_log_start(const char *path, int append)
{
    FILE *fp;

    if (!path || !*path) return -1;

    fp = fopen(path, append ? "a" : "w");
    if (!fp) return -1;

    if (s_logfile) fclose(s_logfile);
    s_logfile = fp;
    snprintf(s_log_path, sizeof s_log_path, "%s", path);
    return 0;
}

void guppy_log_stop(void)
{
    if (s_logfile) {
        fflush(s_logfile);
        fclose(s_logfile);
    }
    s_logfile = NULL;
    s_log_path[0] = '\0';
}

int guppy_log_active(void)
{
    return s_logfile != NULL;
}

const char *guppy_log_path(void)
{
    return s_logfile ? s_log_path : NULL;
}

void guppy_log_command(const char *line)
{
    if (!s_logfile || !line || !*line) return;
    fprintf(s_logfile, ">> %s\n", line);
    fflush(s_logfile);
}

void guppy_log_vwrite(const char *fmt, va_list ap)
{
    if (!s_logfile || !fmt) return;
    vfprintf(s_logfile, fmt, ap);
    fflush(s_logfile);
}

size_t guppy_log_write(const void *buf, size_t len)
{
    size_t written;

    if (!s_logfile || (!buf && len != 0)) return 0;
    written = fwrite(buf, 1, len, s_logfile);
    fflush(s_logfile);
    return written;
}

int guppy_printf(const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = app_vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

int guppy_eprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    error_vprintf(fmt, ap);
    va_end(ap);
    return 0;
}
