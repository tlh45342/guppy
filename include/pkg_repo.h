#pragma once
#include <stddef.h>
#include <stdint.h>

#define PKG_REPO_MAX 8

typedef struct pkg_repo_entry {
    char package[128];
    char version[128];
    char architecture[64];
    char depends[4096];
    char pre_depends[2048];
    char provides[2048];
    char conflicts[2048];
    char breaks[2048];
    char replaces[2048];
    char filename[768];
    char sha256[65];
    uint64_t size;
    char index_path[768];
    char repo_root[512];
} pkg_repo_entry_t;

int pkg_repo_add(const char *root, char *err, size_t errsz);
int pkg_repo_delete(const char *spec, char *err, size_t errsz);
void pkg_repo_list(void);
void pkg_repo_clear(void);
size_t pkg_repo_entry_count(void);
const pkg_repo_entry_t *pkg_repo_entry_at(size_t i);
const pkg_repo_entry_t *pkg_repo_find_best(const char *name, const char *op, const char *version);
const pkg_repo_entry_t *pkg_repo_find_provider(const char *virtual_name);
int pkg_repo_lookup_print(const char *name);
int pkg_repo_resolve_path(const char *name, char *out, size_t outsz);
int pkg_repo_path_print(const char *name);
