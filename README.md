## README.md

# Guppy

Tiny, pragmatic disk-image tinkering tool. Create images, map them to simple `/dev/*` names, lay down MBR/GPT, and (soon) make filesystems — all from a friendly REPL or one-shot CLI. Scriptable.

## Why Guppy?

- **Lightweight**: single binary, C11.
- **Deterministic**: no magic; prints what it writes.
- **Image-first**: safe defaults; works on files, not real disks.
- **Ergonomic**: REPL, scripts, and clear subcommands.

------

## Build

Prereqs: a C toolchain (gcc/clang), `make`.

## WARNING

This is SO alpha.  This is very immature. But... it's starting nicely.  Will be dropping in formating and filesystem commands soon.  This is necessary because "fish" tools generally are not available on Windows.  Plus this is being designed to be a scriptable shell.  So yes, automate the create of an entire system.

## INSTALLATION

```bash
cd /opt
git clone https://github.com/tlh45342/guppy.git
cd guppy
make ; make install
```

## WARNING

This has NOT been tested hardly at all.  Use at your own risk.  Do NOT use this on a production system.

# Guppy

Tiny, pragmatic disk-image tinkering tool. Create images, map them to simple `/dev/*` names, lay down MBR/GPT, and (soon) make filesystems — all from a friendly REPL or one-shot CLI.

## Why Guppy?

- **Lightweight**: single binary, C11.
- **Deterministic**: no magic; prints what it writes.
- **Image-first**: safe defaults; works on files, not real disks.
- **Ergonomic**: REPL, scripts, and clear subcommands.

------

## Build

Prereqs: a C toolchain (gcc/clang), `make`..

------

## Test

Test directory contains testing/automation scripts.