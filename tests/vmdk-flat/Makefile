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
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" vmdk-flat

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vmdk-flat artifacts
endif
	-@del /Q fixed-test.vmdk fixed-test-flat.vmdk vmdk-flat.log 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vmdk-flat artifacts
endif
	@rm -f -- fixed-test.vmdk fixed-test-flat.vmdk vmdk-flat.log
endif
