#pragma once
#include <stdbool.h>

bool devmap_add(const char *dev, const char *image_path);
const char* devmap_resolve(const char *dev);  // returns image path or NULL
void devmap_list(void);