// include/cwd.h

#pragma once

// Returns the current working directory path, e.g. "/"
const char* cwd_get(void);

// Set current directory; normalizes empty -> "/"
void cwd_set(const char *path);