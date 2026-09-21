#include <stdio.h>
#include <stdarg.h>

#include "app_printf.h"
#include "logfile.h"

int app_vprintf(const char *fmt, va_list ap)
{
    va_list log_ap;
    int rc;

    if (!fmt) return -1;

    /* vfprintf() consumes ap, so preserve a copy for the optional log sink. */
    va_copy(log_ap, ap);
    rc = vfprintf(stdout, fmt, ap);
    fflush(stdout);

    guppy_log_vwrite(fmt, log_ap);
    va_end(log_ap);
    return rc;
}

int app_printf(const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = app_vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

size_t app_write(const void *buf, size_t len)
{
    size_t written;

    if (!buf && len != 0) return 0;

    written = fwrite(buf, 1, len, stdout);
    fflush(stdout);

    guppy_log_write(buf, written);
    return written;
}
