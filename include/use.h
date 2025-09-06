// include/use.h

#pragma once
#include <stdbool.h>

bool use_add(char letter, const char *path, bool ro);
bool use_rm(char letter);
bool use_select(char letter);          // sets default device
bool use_get(char letter, const char **out_path);
bool use_get_selected(char *out_letter, const char **out_path);
void use_list(void);
bool resolve_image_or_dev(const char *arg, const char **out_path); // "/dev/a" or "a" => path