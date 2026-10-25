/*
 * cmd_archive.c
 *
 * Host-file archive utilities exposed through Guppy's normal command
 * registry. These commands operate on host paths, not mounted VFS paths.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ar_write.h"
#include "deb_build.h"
#include "tar_write.h"

int gu_xz_encode_file(FILE *src, FILE *dst);
int gu_tar_pack_directory(FILE *out, const char *directory);

static int file_size(FILE *file, uint64_t *size)
{
    long length;

    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        return -1;
    }
    *size = (uint64_t)length;
    return 0;
}

int cmd_dpkg_deb(int argc, char **argv)
{
    int compress;

    if ((argc != 4 && argc != 5) ||
        strcmp(argv[1], "--build") != 0 ||
        (argc == 5 && strcmp(argv[4], "--xz") != 0)) {
        fprintf(stderr,
                "usage: dpkg-deb --build STAGING OUTPUT.deb [--xz]\n");
        return 2;
    }

    compress = argc == 5;
    if (gu_deb_build_ex(argv[2], argv[3], compress) != 0) {
        fprintf(stderr, "dpkg-deb: package build failed\n");
        return 1;
    }
    printf("Built %s\n", argv[3]);
    return 0;
}

/* Create an uncompressed ustar archive from a host directory. */
int cmd_tar(int argc, char **argv)
{
    FILE *out;
    int result;

    if (argc != 4 || strcmp(argv[1], "-cf") != 0) {
        fprintf(stderr, "usage: tar -cf ARCHIVE.tar DIRECTORY\n");
        return 2;
    }

    out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror("tar");
        return 1;
    }

    result = gu_tar_pack_directory(out, argv[3]);
    if (fclose(out) != 0) {
        result = -1;
    }
    if (result != 0) {
        remove(argv[2]);
        fprintf(stderr, "tar: archive creation failed\n");
        return 1;
    }
    return 0;
}

/* Encode a host file as XZ. The input is never modified or removed. */
int cmd_xz(int argc, char **argv)
{
    FILE *in;
    FILE *out;
    int result;

    if (argc != 3) {
        fprintf(stderr, "usage: xz INPUT OUTPUT.xz\n");
        return 2;
    }

    in = fopen(argv[1], "rb");
    if (in == NULL) {
        perror("xz input");
        return 1;
    }

    out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror("xz output");
        fclose(in);
        return 1;
    }

    result = gu_xz_encode_file(in, out);
    if (fclose(out) != 0) {
        result = -1;
    }
    fclose(in);

    if (result != 0) {
        remove(argv[2]);
        fprintf(stderr, "xz: compression failed\n");
        return 1;
    }
    return 0;
}

/* Create an ar archive from named host files. */
int cmd_ar(int argc, char **argv)
{
    FILE *out;
    int result = 0;
    int i;

    if (argc < 4 || strcmp(argv[1], "-q") != 0) {
        fprintf(stderr, "usage: ar -q ARCHIVE.a FILE [FILE ...]\n");
        return 2;
    }

    out = fopen(argv[2], "wb");
    if (out == NULL) {
        perror("ar");
        return 1;
    }
    if (gu_ar_write_begin(out) != 0) {
        result = -1;
    }

    for (i = 3; result == 0 && i < argc; i++) {
        const char *name = strrchr(argv[i], '/');
        const char *backslash = strrchr(argv[i], '\\');
        FILE *in;
        uint64_t size;

        if (backslash != NULL && (name == NULL || backslash > name)) {
            name = backslash;
        }
        name = name == NULL ? argv[i] : name + 1;

        in = fopen(argv[i], "rb");
        if (in == NULL) {
            result = -1;
            break;
        }
        if (file_size(in, &size) != 0 ||
            gu_ar_write_member(out, name, in, size, 0644) != 0) {
            result = -1;
        }
        fclose(in);
    }

    if (fclose(out) != 0) {
        result = -1;
    }
    if (result != 0) {
        remove(argv[2]);
        fprintf(stderr, "ar: archive creation failed\n");
        return 1;
    }
    return 0;
}
