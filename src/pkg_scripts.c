#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pkg_scripts.h"
#include "deb.h"
#include "gu_path.h"
#include "tar.h"
#include "gu_xz.h"
#include "app_printf.h"

static int clean_script_name(const char *p)
{
    while (p[0] == '.' && p[1] == '/') p += 2;
    return !strcmp(p, "preinst") || !strcmp(p, "postinst") ||
           !strcmp(p, "prerm") || !strcmp(p, "postrm") ||
           !strcmp(p, "config");
}

static const char *clean_name(const char *p)
{
    while (p[0] == '.' && p[1] == '/') p += 2;
    return p;
}

static int raw_tar_name(const char *name)
{
    return !strcmp(name, "control.tar");
}

static int xz_tar_name(const char *name)
{
    return !strcmp(name, "control.tar.xz");
}

static FILE *open_control_tar(FILE *deb, const deb_member_t *m,
                              uint64_t *off, uint64_t *len, int *must_close)
{
    if (raw_tar_name(m->name)) {
        *off = m->offset; *len = m->size; *must_close = 0; return deb;
    }
    if (xz_tar_name(m->name)) {
        char err[128]; FILE *f = gu_xz_decode_member(deb, m->offset, m->size, err, sizeof err);
        long n;
        if (!f) { app_printf("pkg scripts: cannot decode %s: %s\n", m->name, err); return NULL; }
        if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
            fclose(f); return NULL;
        }
        *off = 0; *len = (uint64_t)n; *must_close = 1; return f;
    }
    app_printf("pkg scripts: unsupported control archive compression: %s\n", m->name);
    return NULL;
}

static int has_wordish(const char *text, const char *needle)
{
    return strstr(text, needle) != NULL;
}

static void print_signal(const char *name, int yes)
{
    if (yes) app_printf("      %s\n", name);
}

typedef struct scan_ctx { unsigned scripts; } scan_ctx_t;

static int scan_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    scan_ctx_t *ctx = (scan_ctx_t *)vp;
    const char *name = clean_name(e->path);
    char *buf, *line_end;
    size_t n;
    int fs, linker, alternatives, initramfs, services, proc_sys, pkgtools, interpreter;

    if (e->type != '0' || !clean_script_name(name)) return 0;
    if (e->size > 4u * 1024u * 1024u || e->data_offset > LONG_MAX) return 1;
    n = (size_t)e->size;
    buf = (char *)malloc(n + 1u);
    if (!buf) return 1;
    if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 || (n && fread(buf, 1, n, fp) != n)) {
        free(buf); return 1;
    }
    buf[n] = 0;
    ctx->scripts++;

    app_printf("  %s\n", name);
    app_printf("    Size: %llu bytes\n", (unsigned long long)e->size);
    interpreter = n >= 2 && buf[0] == '#' && buf[1] == '!';
    if (interpreter) {
        line_end = strchr(buf, '\n');
        if (line_end) *line_end = 0;
        app_printf("    Interpreter: %s\n", buf + 2);
        if (line_end) *line_end = '\n';
    } else {
        app_printf("    Interpreter: (not declared)\n");
    }

    fs = has_wordish(buf,"mkdir") || has_wordish(buf,"chmod") || has_wordish(buf,"chown") ||
         has_wordish(buf,"ln ") || has_wordish(buf,"ln\t") || has_wordish(buf,"rm ") ||
         has_wordish(buf,"mv ") || has_wordish(buf,"cp ") || has_wordish(buf,"install ") ||
         has_wordish(buf,"touch ");
    linker = has_wordish(buf,"ldconfig");
    alternatives = has_wordish(buf,"update-alternatives");
    initramfs = has_wordish(buf,"update-initramfs") || has_wordish(buf,"mkinitramfs");
    services = has_wordish(buf,"systemctl") || has_wordish(buf,"invoke-rc.d") ||
               has_wordish(buf,"start-stop-daemon") || has_wordish(buf,"deb-systemd-") ||
               has_wordish(buf," service ");
    proc_sys = has_wordish(buf,"/proc") || has_wordish(buf,"/sys") || has_wordish(buf,"udevadm");
    pkgtools = has_wordish(buf,"dpkg-trigger") || has_wordish(buf,"dpkg-maintscript-helper") ||
               has_wordish(buf,"update-ca-certificates") || has_wordish(buf,"ucf ");

    app_printf("    Detected signals:\n");
    if (!(fs||linker||alternatives||initramfs||services||proc_sys||pkgtools))
        app_printf("      (none from current scanner)\n");
    print_signal("filesystem mutation commands", fs);
    print_signal("dynamic-linker cache (ldconfig)", linker);
    print_signal("alternatives management", alternatives);
    print_signal("initramfs generation", initramfs);
    print_signal("service/systemd interaction", services);
    print_signal("live /proc, /sys, or udev interaction", proc_sys);
    print_signal("dpkg/helper tooling", pkgtools);
    app_printf("    Assessment: REVIEW (read-only heuristic; script not executed)\n");
    free(buf);
    return 0;
}

int pkg_scripts_inspect(const char *path)
{
    deb_package_t p; FILE *deb, *tar; uint64_t off, len; int close_tar = 0, rc; scan_ctx_t ctx;
    deb = gu_path_open_seekable_read(path, NULL);
    if (!deb) { app_printf("pkg scripts: cannot open '%s'\n", path); return 1; }
    rc = deb_open_fp(deb, &p);
    if (rc) { fclose(deb); app_printf("pkg scripts: invalid Debian package (rc=%d)\n", rc); return 1; }
    tar = open_control_tar(deb, &p.control, &off, &len, &close_tar);
    if (!tar) { fclose(deb); return 1; }
    memset(&ctx, 0, sizeof ctx);
    app_printf("Maintainer scripts: %s\n", path);
    rc = gu_tar_walk(tar, off, len, scan_cb, &ctx);
    if (close_tar) fclose(tar);
    fclose(deb);
    if (rc) { app_printf("pkg scripts: cannot scan control archive\n"); return 1; }
    if (!ctx.scripts) app_printf("  (none)\n");
    app_printf("Scripts found: %u\n", ctx.scripts);
    app_printf("Execution: DISABLED\n");
    return 0;
}


typedef struct show_ctx {
    const char *wanted;
    unsigned found;
} show_ctx_t;

static int interesting_line(const char *line)
{
    return has_wordish(line, "ldconfig") ||
           has_wordish(line, "update-alternatives") ||
           has_wordish(line, "update-initramfs") || has_wordish(line, "mkinitramfs") ||
           has_wordish(line, "systemctl") || has_wordish(line, "invoke-rc.d") ||
           has_wordish(line, "start-stop-daemon") || has_wordish(line, "deb-systemd-") ||
           has_wordish(line, "/proc") || has_wordish(line, "/sys") || has_wordish(line, "udevadm") ||
           has_wordish(line, "dpkg-trigger") || has_wordish(line, "dpkg-maintscript-helper") ||
           has_wordish(line, "mkdir") || has_wordish(line, "chmod") || has_wordish(line, "chown") ||
           has_wordish(line, "ln ") || has_wordish(line, "ln\\t") || has_wordish(line, "rm ") ||
           has_wordish(line, "mv ") || has_wordish(line, "cp ") || has_wordish(line, "install ") ||
           has_wordish(line, "touch ");
}

static void print_numbered_script(const char *buf, size_t n, int interesting_only)
{
    size_t pos = 0;
    unsigned line_no = 1;
    while (pos < n) {
        size_t start = pos, len;
        char *line;
        while (pos < n && buf[pos] != '\n') pos++;
        len = pos - start;
        if (len && buf[start + len - 1] == '\r') len--;
        line = (char *)malloc(len + 1u);
        if (!line) return;
        memcpy(line, buf + start, len);
        line[len] = 0;
        if (!interesting_only || interesting_line(line))
            app_printf("%5u | %s\n", line_no, line);
        free(line);
        if (pos < n && buf[pos] == '\n') pos++;
        line_no++;
    }
}

static int show_cb(FILE *fp, const gu_tar_entry_t *e, void *vp)
{
    show_ctx_t *ctx = (show_ctx_t *)vp;
    const char *name = clean_name(e->path);
    char *buf;
    size_t n;

    if (e->type != '0' || strcmp(name, ctx->wanted) != 0) return 0;
    if (e->size > 4u * 1024u * 1024u || e->data_offset > LONG_MAX) return 1;
    n = (size_t)e->size;
    buf = (char *)malloc(n + 1u);
    if (!buf) return 1;
    if (fseek(fp, (long)e->data_offset, SEEK_SET) != 0 ||
        (n && fread(buf, 1, n, fp) != n)) {
        free(buf); return 1;
    }
    buf[n] = 0;
    ctx->found = 1;

    app_printf("Maintainer script: %s [%s]\n", ctx->wanted, name);
    app_printf("Size: %llu bytes\n", (unsigned long long)e->size);
    app_printf("Mode: READ-ONLY\nExecution: DISABLED\n\n");
    app_printf("----- BEGIN SCRIPT (line numbered) -----\n");
    print_numbered_script(buf, n, 0);
    app_printf("----- END SCRIPT -----\n\n");
    app_printf("Interesting lines (heuristic index; context above is authoritative):\n");
    print_numbered_script(buf, n, 1);
    app_printf("Execution: DISABLED\n");
    free(buf);
    return 0;
}

int pkg_script_show(const char *path, const char *script_name)
{
    deb_package_t p;
    FILE *deb, *tar;
    uint64_t off, len;
    int close_tar = 0, rc;
    show_ctx_t ctx;

    if (!clean_script_name(script_name)) {
        app_printf("pkg script: unknown maintainer script '%s'\n", script_name);
        app_printf("pkg script: expected preinst, postinst, prerm, postrm, or config\n");
        return 2;
    }
    deb = gu_path_open_seekable_read(path, NULL);
    if (!deb) { app_printf("pkg script: cannot open '%s'\n", path); return 1; }
    rc = deb_open_fp(deb, &p);
    if (rc) { fclose(deb); app_printf("pkg script: invalid Debian package (rc=%d)\n", rc); return 1; }
    tar = open_control_tar(deb, &p.control, &off, &len, &close_tar);
    if (!tar) { fclose(deb); return 1; }
    memset(&ctx, 0, sizeof ctx);
    ctx.wanted = script_name;
    app_printf("Package file: %s\n", path);
    rc = gu_tar_walk(tar, off, len, show_cb, &ctx);
    if (close_tar) fclose(tar);
    fclose(deb);
    if (rc) { app_printf("pkg script: cannot scan control archive\n"); return 1; }
    if (!ctx.found) {
        app_printf("pkg script: %s not present in %s\n", script_name, path);
        return 1;
    }
    return 0;
}
