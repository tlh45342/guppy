/*
 * deb_build.c
 *
 * Assemble a Debian binary package from a staging directory. The DEBIAN
 * subtree supplies control metadata; other files become the data archive.
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ar_write.h"
#include "deb_build.h"
#include "tar_write.h"

#define PACKAGE_PATH_MAX 4096

int gu_xz_encode_file(FILE *src, FILE *dst);

/* Join two path components, rejecting a truncated result. */
static int join(char *out, size_t capacity, const char *a, const char *b)
{
    int length = snprintf(out, capacity, "%s/%s", a, b);

    if (length < 0 || (size_t)length >= capacity) {
        return -1;
    }
    return 0;
}

/*
 * Recursively serialize a directory tree into TAR.
 * For data archives, omit the top-level DEBIAN metadata directory.
 * Symlinks and special files are deliberately unsupported.
 */
static int pack_tree(FILE *tar, const char *root, const char *relative,
                     int control)
{
    char directory_path[PACKAGE_PATH_MAX];
    char child[PACKAGE_PATH_MAX];
    char disk_path[PACKAGE_PATH_MAX];
    struct dirent *entry;
    DIR *directory;

    if (relative[0] != 0) {
        if (join(directory_path, sizeof directory_path, root, relative) != 0) {
            return -1;
        }
    } else {
        int length = snprintf(directory_path, sizeof directory_path,
                              "%s", root);
        if (length < 0 || (size_t)length >= sizeof directory_path) {
            return -1;
        }
    }

    directory = opendir(directory_path);
    if (directory == NULL) {
        return -1;
    }

    while ((entry = readdir(directory)) != NULL) {
        struct stat info;
        FILE *source;
        long size;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!control && relative[0] == 0 &&
            strcmp(entry->d_name, "DEBIAN") == 0) {
            continue;
        }

        if (join(child, sizeof child, relative, entry->d_name) != 0) {
            goto fail;
        }

        /* A top-level archive path must not start with a slash. */
        if (relative[0] == 0) {
            int length = snprintf(child, sizeof child, "%s", entry->d_name);
            if (length < 0 || (size_t)length >= sizeof child) {
                goto fail;
            }
        }

        if (join(disk_path, sizeof disk_path, root, child) != 0 ||
            stat(disk_path, &info) != 0) {
            goto fail;
        }

        if (S_ISDIR(info.st_mode)) {
            if (gu_tar_write_dir(tar, child, 0755) != 0 ||
                pack_tree(tar, root, child, control) != 0) {
                goto fail;
            }
        } else if (S_ISREG(info.st_mode)) {
            unsigned mode = 0644;

            /* Preserve the original executable-mode policy for bin paths. */
            if (!control &&
                (strstr(child, "bin/") == child ||
                 strstr(child, "usr/bin/") == child)) {
                mode = 0755;
            }

            source = fopen(disk_path, "rb");
            if (source == NULL) {
                goto fail;
            }
            if (fseek(source, 0, SEEK_END) != 0 ||
                (size = ftell(source)) < 0 ||
                fseek(source, 0, SEEK_SET) != 0) {
                fclose(source);
                goto fail;
            }
            if (gu_tar_write_file(tar, child, source,
                                  (uint64_t)size, mode) != 0) {
                fclose(source);
                goto fail;
            }
            fclose(source);
        } else {
            goto fail;
        }
    }

    closedir(directory);
    return 0;

fail:
    closedir(directory);
    return -1;
}

/* Determine the temporary TAR stream's size before adding an ar member. */
static int add_member(FILE *out, const char *name, FILE *tar)
{
    long size;

    if (fseek(tar, 0, SEEK_END) != 0 ||
        (size = ftell(tar)) < 0 ||
        fseek(tar, 0, SEEK_SET) != 0) {
        return -1;
    }
    return gu_ar_write_member(out, name, tar, (uint64_t)size, 0644);
}

/*
 * Build a .deb package. If xz is nonzero, compress the two TAR streams.
 * All temporary streams are closed on both success and failure; a failed
 * output archive is removed rather than left as a partial package.
 */
int gu_deb_build_ex(const char *staging, const char *output, int xz)
{
    FILE *control = NULL;
    FILE *data = NULL;
    FILE *out = NULL;
    FILE *check = NULL;
    FILE *version = NULL;
    FILE *compressed_control = NULL;
    FILE *compressed_data = NULL;
    char path[PACKAGE_PATH_MAX];
    int result = -1;

    if (staging == NULL || output == NULL ||
        join(path, sizeof path, staging, "DEBIAN/control") != 0) {
        return -1;
    }

    check = fopen(path, "rb");
    if (check == NULL) {
        return -1;
    }
    if (fseek(check, 0, SEEK_END) != 0 || ftell(check) <= 0) {
        fclose(check);
        return -1;
    }
    fclose(check);

    control = tmpfile();
    data = tmpfile();
    version = tmpfile();
    if (control == NULL || data == NULL || version == NULL) {
        goto done;
    }

    if (join(path, sizeof path, staging, "DEBIAN") != 0 ||
        pack_tree(control, path, "", 1) != 0 ||
        gu_tar_write_end(control) != 0 ||
        pack_tree(data, staging, "", 0) != 0 ||
        gu_tar_write_end(data) != 0) {
        goto done;
    }

    if (xz) {
        compressed_control = tmpfile();
        compressed_data = tmpfile();
        if (compressed_control == NULL || compressed_data == NULL ||
            gu_xz_encode_file(control, compressed_control) != 0 ||
            gu_xz_encode_file(data, compressed_data) != 0) {
            goto done;
        }
    }

    if (fwrite("2.0\n", 1, 4, version) != 4 ||
        fseek(version, 0, SEEK_SET) != 0) {
        goto done;
    }

    out = fopen(output, "wb");
    if (out == NULL) {
        goto done;
    }

    if (gu_ar_write_begin(out) != 0 ||
        gu_ar_write_member(out, "debian-binary", version, 4, 0644) != 0 ||
        add_member(out, xz ? "control.tar.xz" : "control.tar",
                   xz ? compressed_control : control) != 0 ||
        add_member(out, xz ? "data.tar.xz" : "data.tar",
                   xz ? compressed_data : data) != 0) {
        goto done;
    }
    result = 0;

done:
    if (out != NULL) {
        if (fclose(out) != 0) {
            result = -1;
        }
        if (result != 0) {
            remove(output);
        }
    }
    if (control != NULL) {
        fclose(control);
    }
    if (data != NULL) {
        fclose(data);
    }
    if (version != NULL) {
        fclose(version);
    }
    if (compressed_control != NULL) {
        fclose(compressed_control);
    }
    if (compressed_data != NULL) {
        fclose(compressed_data);
    }
    return result;
}

/* Build an uncompressed Debian archive for callers using the legacy API. */
int gu_deb_build(const char *staging, const char *output)
{
    return gu_deb_build_ex(staging, output, 0);
}

/* Archive a host directory without Debian-specific filtering. */
int gu_tar_pack_directory(FILE *out, const char *directory)
{
    if (out == NULL || directory == NULL) {
        return -1;
    }
    if (pack_tree(out, directory, "", 1) != 0) {
        return -1;
    }
    return gu_tar_write_end(out);
}
