# Guppy pkg Phase 1A acceptance tests
#
# Canonical logger-based harness.
#
# Assumptions:
#   - run from O:\guppy\tests\pkg-phase1a
#   - Guppy has already been built at ..\..\bin\guppy.exe
#   - Debian test ISO is H:\debian-13.6.0-amd64-netinst.iso
#
# Targets:
#   make test       synthetic .deb parser/walker + logfile acceptance test
#   make iso-probe  inspect the real Debian ISO for base-files
#   make clean

GUPPY = ..\..\bin\guppy.exe
PYTHON = python
DEBIAN_ISO = H:\debian-13.6.0-amd64-netinst.iso

.PHONY: all test synthetic iso-probe clean

all: test

test: synthetic
	@echo === pkg Phase 1A test PASS ===

synthetic:
	@echo === Building synthetic uncompressed .deb ===
	$(PYTHON) make_test_deb.py
	@echo === Running pkg info / pkg contents with Guppy logfile ===
	$(GUPPY) test.script
	@echo === Checking expected output ===
	$(PYTHON) check_output.py test.log

iso-probe:
	@echo === Probing real Debian 13.6 ISO ===
	@echo ISO: $(DEBIAN_ISO)
	$(GUPPY) iso-probe.script
	@echo The listing above gives the exact base-files package filename carried
	@echo by this ISO. That is the package we will use for the compressed
	@echo data.tar.* acceptance test in Phase 1B.

clean:
	-del /Q test.deb test.log 2>NUL
