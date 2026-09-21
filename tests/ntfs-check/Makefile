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
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" ntfs-check

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning ntfs-check artifacts
endif
	-@del /Q ntfs-check.img ntfs-check-corrupt.img ntfs-check.log 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning ntfs-check artifacts
endif
	@rm -f -- ntfs-check.img ntfs-check-corrupt.img ntfs-check.log
endif
