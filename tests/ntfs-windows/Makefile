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
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" ntfs-windows
	@echo Windows probe image retained: ntfs-windows.vhd
	@echo Attach this VHD with Windows Disk Management and report how Windows identifies the volume.

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning ntfs-windows artifacts
endif
	-@del /Q ntfs-windows.vhd ntfs-windows.log 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning ntfs-windows artifacts
endif
	@rm -f -- ntfs-windows.vhd ntfs-windows.log
endif
