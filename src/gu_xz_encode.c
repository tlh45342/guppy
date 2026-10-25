/*
 * gu_xz_encode.c
 *
 * Encode a seekable input file as an XZ stream using the bundled 7-Zip
 * encoder. The caller retains ownership of both FILE streams.
 */

#include <stdio.h>

#include "7zCrc.h"
#include "Alloc.h"
#include "XzCrc64.h"
#include "XzEnc.h"

/* The SDK stream interface is the first member of each adapter. */
typedef struct {
    ISeqInStream vt;
    FILE *file;
} Input;

typedef struct {
    ISeqOutStream vt;
    FILE *file;
} Output;

/*
 * Read up to *size bytes, reporting the actual count to the encoder.
 * A short read is an error only when the underlying stream reports one.
 */
static SRes read_input(const ISeqInStream *stream, void *buffer, size_t *size)
{
    Input *input = (Input *)stream;
    size_t bytes_read = fread(buffer, 1, *size, input->file);

    *size = bytes_read;

    if (ferror(input->file) != 0) {
        return SZ_ERROR_READ;
    }

    return SZ_OK;
}

/* Write the requested bytes and report the number actually written. */
static size_t write_output(const ISeqOutStream *stream,
                           const void *buffer, size_t size)
{
    Output *output = (Output *)stream;

    return fwrite(buffer, 1, size, output->file);
}

/*
 * Compress src into dst. Input is rewound before encoding, and the
 * destination is flushed before success is reported.
 *
 * Return 0 on success, or -1 for an invalid stream, I/O error, or
 * encoder failure. Neither stream is closed here.
 */
int gu_xz_encode_file(FILE *src, FILE *dst)
{
    Input input;
    Output output;
    CXzProps properties;
    SRes result;

    if (src == NULL || dst == NULL) {
        return -1;
    }

    if (fseek(src, 0, SEEK_SET) != 0) {
        return -1;
    }

    input.vt.Read = read_input;
    input.file = src;

    output.vt.Write = write_output;
    output.file = dst;

    CrcGenerateTable();
    Crc64GenerateTable();

    XzProps_Init(&properties);
    properties.lzma2Props.lzmaProps.level = 5;
    properties.numTotalThreads = 1;

    result = Xz_Encode(&output.vt, &input.vt, &properties, NULL);
    if (result != SZ_OK) {
        fprintf(stderr, "XZ encoder error: %d\n", result);
        return -1;
    }

    if (fflush(dst) != 0) {
        return -1;
    }

    return 0;
}
