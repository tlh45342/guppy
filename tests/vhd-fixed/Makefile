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
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" vhd-fixed

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vhd-fixed artifacts
endif
	-@del /Q fixed-test.vhd vhd-fixed.log 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vhd-fixed artifacts
endif
	@rm -f -- fixed-test.vhd vhd-fixed.log
endif
