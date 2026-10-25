ifeq ($(OS),Windows_NT)
GUPPY ?= ..\..\bin\guppy.exe
PYTHON ?= python
RM_F := del /Q
else
GUPPY ?= ../../bin/guppy
PYTHON ?= python3
RM_F := rm -f
endif

.PHONY: test clean

test: clean
	@$(PYTHON) test_uuid_create.py "$(GUPPY)"

clean:
ifeq ($(OS),Windows_NT)
	-@del /Q uuid-test.vdi uuid-test.vmdk uuid-test-flat.vmdk 2>NUL
else
	-@rm -f uuid-test.vdi uuid-test.vmdk uuid-test-flat.vmdk
endif
