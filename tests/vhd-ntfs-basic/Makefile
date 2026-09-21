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
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" vhd-ntfs-basic

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vhd-ntfs-basic artifacts
endif
	-@del /Q fixed-ntfs.vhd vhd-ntfs-basic.log 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vhd-ntfs-basic artifacts
endif
	@rm -f -- fixed-ntfs.vhd vhd-ntfs-basic.log
endif
