Guppy disk/device application-output regression.

This test exercises commands commonly used by build scripts and verifies that
normal user-visible output is routed through app_printf() into the active
logfile. It covers create, use, GPT init/add/print, partscan, parted reporting,
and help/command-registry output.

Run from the repository root with:
    make test-disk-output

The normal `make test` suite also includes this test.
