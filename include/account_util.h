#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool account_vfs_read_all(const char *path, char **out, size_t *len_out);
bool account_vfs_write_all(const char *path, const char *data, size_t len, uint32_t mode);
bool account_vfs_mkdir_p(const char *path, uint32_t mode);
bool account_join_path(char *out, size_t cap, const char *root, const char *suffix);
bool account_valid_name(const char *name);
int  account_next_uid(const char *passwd_text, uint32_t start_uid, uint32_t *uid_out);
bool account_has_name(const char *text, const char *name);
bool account_replace_named_line(const char *old_text, const char *name,
                                const char *newline, char **out_text);
bool account_append_line(const char *old_text, const char *line, char **out_text);

/* Portable SHA-256 crypt, shadow-compatible "$5$salt$hash". */
bool account_sha256_crypt(const char *password, const char *salt,
                          char *out, size_t out_cap);
