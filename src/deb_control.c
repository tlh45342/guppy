#include <string.h>
#include <ctype.h>
#include "deb_control.h"

static void copy_value(char *dst, size_t cap, const char *p, size_t n)
{
    if (!dst || cap == 0) return;
    while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n' ||
                 p[n - 1] == ' ' || p[n - 1] == '\t'))
        n--;
    if (n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = 0;
}

static char *field(deb_control_t *c, const char *name, size_t n, size_t *cap)
{
#define F(k, m) if (n == sizeof(k)-1 && !memcmp(name, k, sizeof(k)-1)) { *cap = sizeof(c->m); return c->m; }
    F("Package", package)
    F("Version", version)
    F("Architecture", architecture)
    F("Essential", essential)
    F("Priority", priority)
    F("Section", section)
    F("Maintainer", maintainer)
    F("Installed-Size", installed_size)
    F("Pre-Depends", pre_depends)
    F("Depends", depends)
    F("Recommends", recommends)
    F("Suggests", suggests)
    F("Description", description)
#undef F
    *cap = 0;
    return NULL;
}

static void append_continuation(char *dst, size_t cap, const char *p, size_t n)
{
    size_t used;
    if (!dst || cap == 0) return;
    while (n && (*p == ' ' || *p == '\t')) { p++; n--; }
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n')) n--;

    used = strlen(dst);
    if (used && used + 1 < cap) dst[used++] = '\n';
    if (n > cap - used - 1) n = cap - used - 1;
    memcpy(dst + used, p, n);
    dst[used + n] = 0;
}

int deb_control_parse(const char *text, size_t len, deb_control_t *out)
{
    size_t pos = 0;
    char *current = NULL;
    size_t current_cap = 0;

    if (!text || !out) return -1;
    memset(out, 0, sizeof *out);

    while (pos < len) {
        size_t start = pos, end, colon;
        while (pos < len && text[pos] != '\n') pos++;
        end = pos;
        if (pos < len) pos++;
        if (end > start && text[end - 1] == '\r') end--;

        if (end == start) {
            current = NULL;
            current_cap = 0;
            continue;
        }

        if (text[start] == ' ' || text[start] == '\t') {
            append_continuation(current, current_cap, text + start, end - start);
            continue;
        }

        colon = start;
        while (colon < end && text[colon] != ':') colon++;
        if (colon == end) {
            current = NULL;
            current_cap = 0;
            continue;
        }

        current = field(out, text + start, colon - start, &current_cap);
        if (current)
            copy_value(current, current_cap, text + colon + 1, end - colon - 1);
    }

    return out->package[0] && out->version[0] ? 0 : -2;
}
