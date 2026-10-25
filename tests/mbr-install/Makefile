ifeq ($(OS),Windows_NT)
  GUPPY ?= ..\..\bin\guppy.exe
  PYTHON ?= python
else
  GUPPY ?= ../../bin/guppy
  PYTHON ?= python3
endif

.PHONY: test clean

test: clean
	@$(PYTHON) -c "from pathlib import Path; Path('boot.bin').write_bytes(bytes(((i*37+11)&255) for i in range(173)))"
	@$(GUPPY) test.script
	@$(PYTHON) -c "from pathlib import Path; Path('lba0-before.bin').write_bytes(Path('mbr-install.img').read_bytes()[:512])" 
	@$(GUPPY) install.script
	@$(GUPPY) reopen.script
	@$(PYTHON) verify.py

clean:
	-@$(PYTHON) -c "from pathlib import Path; [p.unlink() for p in map(Path,['mbr-install.img','mbr-install.log','mbr-install-reopen.log','boot.bin','lba0-before.bin']) if p.exists()]"
