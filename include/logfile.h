#pragma once

#include <stdarg.h>
#include <stddef.h>

/* Host-side Guppy session logging.
 *
 * logfile is deliberately independent of the VFS: logs are test/build
 * artifacts on the host.  Existing commands may continue to use printf();
 * commands migrated to guppy_printf()/guppy_eprintf() are mirrored into the
 * active logfile as well as the console.
 */
int  guppy_log_start(const char *path, int append);
void guppy_log_stop(void);
int  guppy_log_active(void);
const char *guppy_log_path(void);
void guppy_log_command(const char *line);
void guppy_log_vwrite(const char *fmt, va_list ap);
size_t guppy_log_write(const void *buf, size_t len);

int guppy_printf(const char *fmt, ...);
int guppy_eprintf(const char *fmt, ...);
