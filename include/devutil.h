#pragma once
#include <stdbool.h>
#include <stddef.h>

// Parse /dev/a or /dev/aN.
// - base_out: receives "/dev/a"
// - part_out: receives N (0 if no digits)
// Returns true on parse success (prefix "/dev/"), false otherwise.
bool dev_split(const char *dev, char *base_out, size_t base_cap, int *part_out);