ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= ..\..\bin\guppy.exe
else
  PYTHON ?= python3
  GUPPY ?= ../../bin/guppy
endif
ISO ?= H:\debian-13.6.0-amd64-netinst.iso
VERBOSE ?= 1

.PHONY: study clean

study:
	@$(PYTHON) study.py --guppy "$(GUPPY)" --iso "$(ISO)"

clean:
	@$(PYTHON) -c "from pathlib import Path; [p.unlink() for p in [Path('study.generated.script'),Path('debian-study.log'),Path('debian-study-summary.csv'),Path('debian-study-summary.txt')] if p.exists()]"
ifeq ($(VERBOSE),1)
	@echo [CLEAN] removed Debian study generated artifacts
endif
