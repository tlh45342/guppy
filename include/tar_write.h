#pragma once
#include <stdio.h>
#include <stdint.h>
/* USTAR regular files and directories. Paths are relative, slash-separated. */
int gu_tar_write_file(FILE *archive, const char *path, FILE *source, uint64_t size, unsigned mode);
int gu_tar_write_dir(FILE *archive, const char *path, unsigned mode);
int gu_tar_write_end(FILE *archive);
