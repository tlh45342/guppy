# EXT2 mknod regression: create, reopen, and independently inspect device inodes.
VERBOSE ?= 1
ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= ..\..\bin\guppy.exe
else
  PYTHON ?= python3
  GUPPY ?= ../../bin/guppy
endif

.PHONY: all test clean
all: test

test: clean
	@$(GUPPY) create.script
	@$(GUPPY) reopen.script
	@$(PYTHON) verify.py
	@$(PYTHON) -c "from pathlib import Path; s=Path('ext2-mknod-reopen.log').read_text(errors='replace'); need=['Type: char','Major: 1','Minor: 3','Type: block','Major: 8','Blocks: 0']; missing=[x for x in need if x not in s]; print('[VERIFY] reopened VFS stat output PASS' if not missing else '[VERIFY] missing: '+', '.join(missing)); raise SystemExit(bool(missing))"

clean:
	@$(PYTHON) -c "from pathlib import Path; [(p.unlink()) for p in map(Path,['ext2-mknod.img','ext2-mknod-create.log','ext2-mknod-reopen.log']) if p.exists()]"
ifeq ($(VERBOSE),1)
	@echo [CLEAN] removed ext2-mknod test artifacts
endif
