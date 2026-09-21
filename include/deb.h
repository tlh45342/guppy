#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct deb_member {
    char name[64];
    uint64_t offset;
    uint64_t size;
} deb_member_t;

typedef struct deb_package {
    char version[32];
    deb_member_t control;
    deb_member_t data;
} deb_package_t;

int deb_open_fp(FILE *fp, deb_package_t *pkg);
int deb_open(const char *path, deb_package_t *pkg);
int deb_copy_member(FILE *fp, const deb_member_t *m, FILE *out);
