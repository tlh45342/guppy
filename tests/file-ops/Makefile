# Local test contract for the file-ops regression.
#
# From tests/file-ops:
#     make test
#     make clean
#
# VERBOSE controls routine successful housekeeping output only.  It never
# changes whether cleanup or verification happens.

VERBOSE ?= 1

ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= ..\\..\\bin\\guppy.exe
else
  PYTHON ?= python3
  GUPPY ?= ../../bin/guppy
endif

IMAGE := file-ops.img
LOG   := file-ops.log
SCRIPT := test.script

.PHONY: all test clean

all: test

test: clean
	@$(GUPPY) $(SCRIPT)
	@$(PYTHON) -c "from pathlib import Path; s=Path('$(LOG)').read_text(errors='replace'); expected=['/target/work','alpha-version','Path: source.txt','Type: file','Mode: 644','recreated-copy','/target/work/nested']; missing=[x for x in expected if x not in s]; print('[VERIFY] $(LOG): expected output present' if not missing else '[VERIFY] missing expected logfile text: ' + ', '.join(missing)); raise SystemExit(1 if missing else 0)"

clean:
	@$(PYTHON) -c "from pathlib import Path; [(p.unlink()) for p in (Path('$(IMAGE)'), Path('$(LOG)')) if p.exists()]"
ifeq ($(VERBOSE),1)
	@echo [CLEAN] removed file-ops test artifacts
endif
