# Guppy regression test coordinator.
#
# Normal regression suite is self-contained:
#     make test
#
# Real Debian ISO acceptance is intentionally separate:
#     make test-debian
#
# Complete stdout/stderr for every test is captured under logs/.
# logs/regression.log is the single file to keep/send for audit.

ifeq ($(OS),Windows_NT)
  PYTHON ?= python
  GUPPY ?= guppy.exe
else
  PYTHON ?= python3
  GUPPY ?= ../bin/guppy
endif
# Routine local housekeeping is chatty by default.  Parent coordinators may
# pass VERBOSE=0 to suppress successful child-clean announcements; errors and
# test results are never controlled by this flag.
VERBOSE ?= 1

ISO ?= H:\debian-13.6.0-amd64-netinst.iso
REFERENCE_ISO ?= media/guppy-reference.iso

.PHONY: test test-pkg test-pkg-scripts test-pkg-script-show test-pkg-preunpack test-pkg-dash-preunpack test-pkg-base-passwd-preunpack test-pkg-base-files-preunpack test-pkg-libc6-configure test-pkg-configure test-pkg-repo-lifecycle test-pkg-repo-gzip test-pkg-plan test-pkg-auto-install test-pkg-audit test-pkg-plan-conflict test-app-printf test-symlink test-file-ops test-ext2-mknod test-vmdk-flat test-vmdk-boot test-vmdk-layouts test-vhd-fixed test-vhd-ntfs-basic test-ntfs-windows test-vdi-fixed test-vdi-sparse test-vdi-sparse-scale test-vdi-sparse-torture test-diskq test-disk-output test-iso test-debian study-debian clean

test:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" --suite default

test-pkg:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg

test-pkg-scripts:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-scripts

test-pkg-script-show:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-script-show

test-pkg-preunpack:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-preunpack

test-pkg-dash-preunpack:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-dash-preunpack

test-pkg-base-passwd-preunpack:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-base-passwd-preunpack

test-pkg-base-files-preunpack:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-base-files-preunpack

test-pkg-libc6-configure:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-libc6-configure

test-pkg-configure:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" pkg-configure

test-pkg-repo-lifecycle:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-repo-lifecycle

test-pkg-repo-gzip:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" pkg-repo-gzip

test-pkg-plan:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" pkg-plan

test-pkg-auto-install:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-auto-install

test-pkg-audit:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --reference-iso "$(REFERENCE_ISO)" pkg-audit

test-pkg-plan-conflict:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" pkg-plan-conflict

test-app-printf:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" app-printf

test-symlink:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" symlink

test-file-ops:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" file-ops

test-ext2-mknod:
	@$(MAKE) --no-print-directory -C ext2-mknod test GUPPY="$(abspath $(GUPPY))"

test-vmdk-flat:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vmdk-flat

test-vmdk-boot:
	@$(MAKE) --no-print-directory -C vmdk-boot test GUPPY="$(GUPPY)"

test-vmdk-layouts:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vmdk-monolithic-flat-scaffold vmdk-monolithic-sparse-scaffold vmdk-split-flat-scaffold vmdk-split-sparse-scaffold

test-vhd-ntfs-basic:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vhd-ntfs-basic

test-ntfs-windows:
	@$(MAKE) --no-print-directory -C ntfs-windows test GUPPY="$(GUPPY)"

test-ntfs-check:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" ntfs-check

test-vhd-fixed:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vhd-fixed

test-vdi-fixed:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vdi-fixed

test-vdi-sparse:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vdi-sparse

test-vdi-sparse-scale:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vdi-sparse-scale

test-vdi-sparse-torture:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" vdi-sparse-torture

test-diskq:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" diskq

test-disk-output:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" disk-output

test-iso:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" iso

test-debian:
	@$(PYTHON) run_regression.py --guppy "$(GUPPY)" --suite debian

study-debian:
	@$(PYTHON) debian-study/study.py --guppy "$(GUPPY)" --iso "$(ISO)"

clean:
ifeq ($(VERBOSE),1)
	@echo Cleaning test artifacts
endif
	-@$(MAKE) --no-print-directory -C file-ops clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C ext2-mknod clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C pkg clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-scripts clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-script-show clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-preunpack clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-dash-preunpack clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-base-passwd-preunpack clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-base-files-preunpack clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-libc6-configure clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-repo-lifecycle clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-auto-install clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C pkg-audit clean VERBOSE=0
	-@$(MAKE) --no-print-directory -C debian-study clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C diskq clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vmdk-flat clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vmdk-boot clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vmdk-layouts clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vhd-fixed clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vhd-ntfs-basic clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C ntfs-windows clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C ntfs-check clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vdi-sparse-scale clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C vdi-sparse-torture clean VERBOSE=$(VERBOSE)
	-@$(MAKE) --no-print-directory -C pkg-deb-test clean VERBOSE=$(VERBOSE)
	@$(PYTHON) -c "from pathlib import Path; import shutil; shutil.rmtree(Path('logs'), ignore_errors=True); [p.unlink() for p in Path('.').rglob('*.log') if p.is_file()]"
	@$(PYTHON) clean_artifacts.py
