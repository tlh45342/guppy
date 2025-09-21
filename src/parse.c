// src/parse.c
#include "parse.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- comment helpers ---------------- */

// returns 1 if the line is blank or a full-line '#' comment
static int is_blank_or_hash_comment(const char *line){
    const char *s = line;
    while (*s == ' ' || *s == '\t') s++;   // skip leading whitespace
    return (*s == '\0' || *s == '#');
}

// remove inline '#' comments, honoring quotes and backslash escapes
static void strip_inline_hash_comment(char *line){
    int in_single = 0, in_double = 0, esc = 0;
    for (char *p = line; *p; ++p) {
        if (esc) { esc = 0; continue; }
        if (*p == '\\') { esc = 1; continue; }
        if (!in_double && *p == '\'') { in_single = !in_single; continue; }
        if (!in_single && !in_double && *p == '\"') { in_double = !in_double; continue; }
        if (!in_single && !in_double && *p == '#') { *p = '\0'; break; }
    }
}

// preprocess: skip blank/# lines, strip inline '# ...'
static int preprocess_script_line(char *line){
    if (is_blank_or_hash_comment(line)) return 0;
    strip_inline_hash_comment(line);
    return 1;
}

/* case-insensitive string equality */
static int eqi(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* --------------------------------------------------------------------------
   Size parsing
   -------------------------------------------------------------------------- */
static long long suffix_multiplier(const char *s) {
    if (!s || !*s) return 1;

    if (eqi(s, "k")   || eqi(s, "kb")  || eqi(s, "kib"))  return 1024LL;
    if (eqi(s, "m")   || eqi(s, "mb")  || eqi(s, "mib"))  return 1024LL * 1024LL;
    if (eqi(s, "g")   || eqi(s, "gb")  || eqi(s, "gib"))  return 1024LL * 1024LL * 1024LL;

    if (eqi(s, "ki")) return 1024LL;
    if (eqi(s, "mi")) return 1024LL * 1024LL;
    if (eqi(s, "gi")) return 1024LL * 1024LL * 1024LL;

    return 0; // unknown suffix
}

long long parse_size_bytes(const char *text) {
    if (!text || !*text) return -1;

    char *endp = NULL;
    long long val = strtoll(text, &endp, 10);
    if (endp == text) return -1; // no digits

    while (*endp == ' ') endp++;

    char suf[8] = {0};
    size_t i = 0;
    while (*endp && i < sizeof(suf)-1 && isalpha((unsigned char)*endp)) {
        suf[i++] = *endp++;
    }
    suf[i] = '\0';

    if (*endp != '\0') return -1; // junk after suffix

    long long mult = 1;
    if (suf[0]) {
        mult = suffix_multiplier(suf);
        if (mult == 0) return -1; // bad suffix
    }

    if (val < 0) return -1;
    return val * mult;
}

long long parse_size_arg(const char *arg) {
    if (!arg) return -1;
    const char *eq = strchr(arg, '=');
    if (starts_with(arg, "--size")) {   // implemented in helper.c
        if (eq && *(eq+1)) return parse_size_bytes(eq+1);
        return -1; // caller may handle "--size" followed by next token
    }
    return parse_size_bytes(arg);
}

/* --------------------------------------------------------------------------
   argv parsing
   -------------------------------------------------------------------------- */
int parse_argv(char *line, int maxv, char **argv) {
    // strip trailing CR/LF first
    size_t n = strlen(line);
    while (n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n] = '\0';

    // handle comments (only '#') and inline '# ...'
    if (!preprocess_script_line(line)) return 0;

    int argc = 0;
    char *p = line;
    while (*p && argc < maxv) {
        // skip spaces
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char *start = p;
        char quote = 0;
        if (*p=='"' || *p=='\'') { quote = *p; start = ++p; }

        char *out = start;
        while (*p) {
            if (quote) {
                if (*p == quote) { p++; break; }
                *out++ = *p++;
            } else {
                if (isspace((unsigned char)*p)) break;
                *out++ = *p++;
            }
        }
        *out = '\0';
        argv[argc++] = start;

        while (*p && !isspace((unsigned char)*p)) p++;
    }
    return argc;
}
