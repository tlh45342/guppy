#pragma once

#include <stdarg.h>
#include <stddef.h>

/*
 * Application-visible formatted output.
 *
 * app_printf() is the normal entry point for application output.
 * app_vprintf() provides the same behavior for callers that already have
 * a va_list, following the printf()/vprintf() convention from C.
 *
 * Output routing is application policy.  In Guppy the current policy is:
 *   - always write to stdout
 *   - mirror to the active host-side logfile, when one is enabled
 */
int app_printf(const char *fmt, ...);
int app_vprintf(const char *fmt, va_list ap);

/* Binary-safe application output for commands such as cat. */
size_t app_write(const void *buf, size_t len);
