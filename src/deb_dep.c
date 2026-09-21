#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "deb_dep.h"

static void seterr(char *e, size_t n, const char *s)
{
    if (e && n) snprintf(e, n, "%s", s);
}

static void trimcopy(char *d, size_t cap, const char *a, const char *b)
{
    size_t n;
    while (a < b && isspace((unsigned char)*a)) a++;
    while (b > a && isspace((unsigned char)b[-1])) b--;
    n = (size_t)(b - a);
    if (n >= cap) n = cap - 1;
    memcpy(d, a, n);
    d[n] = 0;
}

static int parse_alt(const char *a, const char *b, deb_dep_alt_t *o)
{
    const char *p = a, *s, *q;
    memset(o, 0, sizeof *o);
    while (p < b && isspace((unsigned char)*p)) p++;
    s = p;
    while (p < b && (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')) p++;
    if (p == s) return -1;
    trimcopy(o->name, sizeof o->name, s, p);

    if (p < b && *p == ':') {
        p++; s = p;
        while (p < b && (isalnum((unsigned char)*p) || *p == '-')) p++;
        trimcopy(o->arch_qual, sizeof o->arch_qual, s, p);
    }
    while (p < b && isspace((unsigned char)*p)) p++;
    if (p < b && *p == '(') {
        p++;
        while (p < b && isspace((unsigned char)*p)) p++;
        s = p;
        while (p < b && (*p == '<' || *p == '>' || *p == '=')) p++;
        trimcopy(o->op, sizeof o->op, s, p);
        while (p < b && isspace((unsigned char)*p)) p++;
        s = p;
        while (p < b && *p != ')') p++;
        if (p >= b) return -2;
        q = p;
        while (q > s && isspace((unsigned char)q[-1])) q--;
        trimcopy(o->version, sizeof o->version, s, q);
        p++;
    }
    while (p < b && isspace((unsigned char)*p)) p++;
    if (p < b) trimcopy(o->restrictions, sizeof o->restrictions, p, b);
    return 0;
}

int deb_dep_parse(const char *text, deb_dep_list_t *out, char *err, size_t errsz)
{
    const char *p, *g, *end;
    int par = 0, br = 0, profile = 0;
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (err && errsz) *err = 0;
    if (!text || !*text) return 0;
    p = g = text;
    end = text + strlen(text);

    for (;;) {
        char c = (p < end) ? *p : ',';
        if (c == '(') par++;
        else if (c == ')' && par) par--;
        else if (c == '[') br++;
        else if (c == ']' && br) br--;
        else if (c == '<' && !par) profile++;
        else if (c == '>' && profile && !par) profile--;

        if (c == ',' && !par && !br && !profile) {
            const char *x = g, *a = g;
            deb_dep_group_t *grp;
            int p2 = 0, b2 = 0, pr2 = 0;
            if (out->group_count >= DEB_DEP_MAX_GROUPS) {
                seterr(err, errsz, "too many dependency groups"); return -2;
            }
            grp = &out->group[out->group_count++];
            while (x <= p) {
                char z = (x < p) ? *x : '|';
                if (z == '(') p2++;
                else if (z == ')' && p2) p2--;
                else if (z == '[') b2++;
                else if (z == ']' && b2) b2--;
                else if (z == '<' && !p2) pr2++;
                else if (z == '>' && pr2 && !p2) pr2--;
                if (z == '|' && !p2 && !b2 && !pr2) {
                    if (grp->alt_count >= DEB_DEP_MAX_ALTS) {
                        seterr(err, errsz, "too many alternatives"); return -3;
                    }
                    if (parse_alt(a, x, &grp->alt[grp->alt_count++])) {
                        seterr(err, errsz, "invalid dependency term"); return -4;
                    }
                    a = x + 1;
                }
                x++;
            }
            g = p + 1;
        }
        if (p >= end) break;
        p++;
    }
    return 0;
}
