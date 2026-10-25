# Bundled XZ/LZMA2 encoder

These C sources and headers are vendored from Igor Pavlov's LZMA SDK (public domain).
Upstream mirror: https://github.com/jljusten/LZMA-SDK
Pinned source commit: `781863cdf592da3e97420f50de5dac056ad352a5`.

Guppy's portable adapter: `src/gu_xz_encode.c`. The Makefile compiles the selected C sources directly into the executable, with `_7ZIP_ST` for the single-threaded encoder. No liblzma DLL, system liblzma, Python, or xz executable is needed at runtime.

Run `python O:/x86/exemplar/repo/test_native_xz_install.py` on the Windows development host to check native XZ output against Python's independent decoder, install the package into a fresh ext2 image, compare SHA-256 digests, and run fsck. Linux and macOS build/runtime validation remains outstanding.
