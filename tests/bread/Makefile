ifeq ($(OS),Windows_NT)
  GUPPY ?= ..\..\bin\guppy.exe
  PYTHON ?= python
else
  GUPPY ?= ../../bin/guppy
  PYTHON ?= python3
endif

.PHONY: test clean

test: clean
	@$(GUPPY) test.script
	@$(PYTHON) verify.py

clean:
	-@$(PYTHON) -c "from pathlib import Path; [p.unlink() for p in [Path('bread.img'),Path('bread.log')] if p.exists()]"
