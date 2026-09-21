ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= guppy.exe
else
  PYTHON ?= python3
  GUPPY ?= guppy
endif
X86_AS ?= x86-as
VERBOSE ?= 1

.PHONY: all test clean

all: test

boot.bin: boot.asm
	$(X86_AS) -f bin boot.asm -o boot.bin
	@$(PYTHON) -c "from pathlib import Path; b=Path('boot.bin').read_bytes(); assert len(b)==512, 'boot.bin must be exactly 512 bytes'; assert b[510:512]==bytes((0x55,0xAA)), 'missing 55 AA boot signature'; print('Built boot.bin: 512-byte BIOS boot sector')"

test: boot.bin
	@$(PYTHON) ../run_regression.py --guppy "$(GUPPY)" vmdk-boot

ifeq ($(OS),Windows_NT)
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vmdk-boot artifacts
endif
	-@del /Q boot.bin boot-test.vmdk boot-test-flat.vmdk vmdk-boot.log 2>NUL
else
clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning vmdk-boot artifacts
endif
	@rm -f -- boot.bin boot-test.vmdk boot-test-flat.vmdk vmdk-boot.log
endif
