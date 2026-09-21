ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= guppy.exe
else
  PYTHON ?= python3
  GUPPY ?= guppy
endif
VERBOSE ?= 1

.PHONY: test clean

test:
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" vmdk-monolithic-flat-scaffold vmdk-monolithic-sparse-scaffold vmdk-split-flat-scaffold vmdk-split-sparse-scaffold

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vmdk-layout scaffold artifacts
endif
	-@del /Q not-yet.vmdk not-yet-flat.vmdk 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vmdk-layout scaffold artifacts
endif
	@rm -f -- not-yet.vmdk not-yet-flat.vmdk
endif
