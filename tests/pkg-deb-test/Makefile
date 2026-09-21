# Private real-Debian Guppy package planner test
#
# Requires:
#   H:\debian-13.6.0-amd64-netinst.iso
#   guppy.exe on PATH
#
# Creates:
#   pkg-real-debian.vdi
#   guppy-pkg-real-debian.log

ISO    := H:\debian-13.6.0-amd64-netinst.iso
IMAGE  := pkg-real-debian.vdi
LOG    := guppy-pkg-real-debian.log
SCRIPT := test-pkg-real-debian.script

.PHONY: all check test clean

VERBOSE ?= 1

all: test

check:
	@if not exist "$(ISO)" (echo [ERROR] Missing Debian ISO: $(ISO) & exit /B 1)
	@where guppy >NUL 2>&1 || (echo [ERROR] guppy.exe not found on PATH & exit /B 1)
	@echo [CHECK] Debian ISO found
	@echo [CHECK] guppy found

test: check
	@echo [TEST] real Debian repository/planner integration
	@guppy $(SCRIPT)
	@echo [TEST] log: $(LOG)

clean:
	-@del /Q "$(IMAGE)" 2>NUL
	-@del /Q "$(LOG)" 2>NUL
ifeq ($(VERBOSE),1)
	@echo [CLEAN] removed disposable package-planner artifacts
endif
