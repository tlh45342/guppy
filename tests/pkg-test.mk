# Shared local Makefile body for package regressions backed by reference media.
# The including Makefile must define TEST_NAME and ARTIFACTS.

VERBOSE ?= 1
ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= ..\bin\guppy.exe
  RUN_REGRESSION := ..\run_regression.py
  REF_HELPER := ..\reference_fixtures.py
else
  PYTHON ?= python3
  GUPPY ?= ../bin/guppy
  RUN_REGRESSION := ../run_regression.py
  REF_HELPER := ../reference_fixtures.py
endif
REFERENCE_ISO ?= ../media/guppy-reference.iso

.PHONY: all test clean
all: test

test: clean
	@$(PYTHON) $(RUN_REGRESSION) --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" $(TEST_NAME)

clean:
	@$(PYTHON) $(REF_HELPER) clean $(TEST_NAME) --cwd .
	@$(PYTHON) -c "from pathlib import Path; [p.unlink() for p in [Path(x) for x in r'$(ARTIFACTS)'.split()] if p.exists()]"
ifeq ($(VERBOSE),1)
	@echo [CLEAN] removed $(TEST_NAME) staged fixtures and artifacts
endif
