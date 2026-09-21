#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "deb_version.h"

static int order_char(unsigned char c)
{
    if (c == 0) return 0;
    if (c == '~') return -1;
    if (isalnum(c)) return (int)c;
    return (int)c + 256;
}

static int verrevcmp(const char *a, const char *b)
{
    while (*a || *b) {
        while ((*a && !isdigit((unsigned char)*a)) ||
               (*b && !isdigit((unsigned char)*b))) {
            int oa = order_char((unsigned char)*a);
            int ob = order_char((unsigned char)*b);
            if (oa != ob) return oa < ob ? -1 : 1;
            if (*a) a++;
            if (*b) b++;
        }

        while (*a == '0') a++;
        while (*b == '0') b++;
        {
            const char *aa = a, *bb = b;
            while (isdigit((unsigned char)*aa)) aa++;
            while (isdigit((unsigned char)*bb)) bb++;
            if ((aa - a) != (bb - b)) return (aa - a) < (bb - b) ? -1 : 1;
            if (aa != a) {
                int c = strncmp(a, b, (size_t)(aa - a));
                if (c) return c < 0 ? -1 : 1;
            }
            a = aa; b = bb;
        }
    }
    return 0;
}

static unsigned long parse_epoch(const char *s, const char **rest)
{
    const char *c = strchr(s, ':');
    unsigned long e = 0;
    if (!c) { *rest = s; return 0; }
    {
        const char *p;
        for (p = s; p < c; p++) if (!isdigit((unsigned char)*p)) { *rest = s; return 0; }
        e = strtoul(s, NULL, 10);
    }
    *rest = c + 1;
    return e;
}

int deb_version_compare(const char *a, const char *b)
{
    const char *ar, *br, *adash, *bdash;
    unsigned long ae, be;
    char au[256], bu[256];
    size_t an, bn;
    int c;
    if (!a) a = "";
    if (!b) b = "";
    ae = parse_epoch(a, &ar); be = parse_epoch(b, &br);
    if (ae != be) return ae < be ? -1 : 1;

    adash = strrchr(ar, '-'); bdash = strrchr(br, '-');
    an = adash ? (size_t)(adash - ar) : strlen(ar);
    bn = bdash ? (size_t)(bdash - br) : strlen(br);
    if (an >= sizeof au) an = sizeof au - 1;
    if (bn >= sizeof bu) bn = sizeof bu - 1;
    memcpy(au, ar, an); au[an] = 0;
    memcpy(bu, br, bn); bu[bn] = 0;
    c = verrevcmp(au, bu); if (c) return c;
    return verrevcmp(adash ? adash + 1 : "0", bdash ? bdash + 1 : "0");
}

int deb_version_satisfies(const char *have, const char *op, const char *want)
{
    int c;
    if (!op || !*op || !want || !*want) return 1;
    c = deb_version_compare(have, want);
    if (!strcmp(op, "=")) return c == 0;
    if (!strcmp(op, ">=") || !strcmp(op, ">>=")) return c >= 0;
    if (!strcmp(op, "<=" ) || !strcmp(op, "<<=")) return c <= 0;
    if (!strcmp(op, ">>")) return c > 0;
    if (!strcmp(op, "<<")) return c < 0;
    return 0;
}
