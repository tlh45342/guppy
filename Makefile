# -----------------------------------------------------------------------------
# Makefile - guppy
# -----------------------------------------------------------------------------
CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra
OPT      ?= -O2
CPPFLAGS ?= -Iinclude -Isrc -Ilib/vdisk/include -Ilib/xz -DXZ_USE_CRC64
LDFLAGS  ?=
LDLIBS   ?=

ifeq ($(OS),Windows_NT)
  CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
endif

ifeq ($(OS),Windows_NT)
  MKDIR_P := mkdir
else
  MKDIR_P := mkdir -p
endif
RM_RF    ?= rm -rf
RM_F     ?= rm -f
CP       ?= cp -f
STRIP    ?= strip

IS_WINDOWS := 0
EXE :=
NULLDEV := /dev/null

ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
  EXE := .exe
  NULLDEV := NUL
  CC := gcc
  PYTHON ?= python
else
  CC := cc
  PYTHON ?= python3
endif

ifeq ($(OS),Windows_NT)
  PREFIX ?= $(USERPROFILE)/.local
else
  PREFIX ?= /usr/local
endif

BINDIR     ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR     ?= $(PREFIX)/lib
DESTDIR    ?=
INSTALL_DIR ?= $(BINDIR)

ISO ?= H:\debian-13.6.0-amd64-netinst.iso

ifneq ($(IS_WINDOWS),1)
  UNAME_S := $(shell uname -s 2>$(NULLDEV) || echo Unknown)
  ifeq ($(UNAME_S),Linux)
    CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
  endif
  ifeq ($(UNAME_S),Darwin)
    CPPFLAGS += -D_DARWIN_C_SOURCE -D_FILE_OFFSET_BITS=64
  endif
  ifneq (,$(findstring MINGW,$(UNAME_S)))
    CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
  endif
endif

SRC_DIR   := src
INC_DIR   := include
BUILD_DIR := build
BIN_DIR   := bin
VDISK_DIR := lib/vdisk
VDISK_LIB := $(VDISK_DIR)/libvdisk.a
VDISK_DEPS := $(wildcard $(VDISK_DIR)/src/*.c) $(wildcard $(VDISK_DIR)/src/*.h) $(wildcard $(VDISK_DIR)/include/*.h) $(VDISK_DIR)/Makefile

BIN := $(BIN_DIR)/guppy$(EXE)
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
XZ_DIR := lib/xz
XZ_SRCS := $(XZ_DIR)/xz_crc32.c $(XZ_DIR)/xz_crc64.c $(XZ_DIR)/xz_dec_lzma2.c $(XZ_DIR)/xz_dec_stream.c
XZ_OBJS := $(patsubst $(XZ_DIR)/%.c,$(BUILD_DIR)/xz_%.o,$(XZ_SRCS))
DEPS := $(OBJS:.o=.d) $(XZ_OBJS:.o=.d)

.PHONY: all clean distclean run test test-pkg-repo-lifecycle test-pkg-repo-gzip test-pkg-plan test-pkg-auto-install test-pkg-audit test-pkg-dash-preunpack test-pkg-base-passwd-preunpack test-pkg-base-files-preunpack test-pkg-libc6-configure test-pkg-configure test-pkg-plan-conflict test-file-ops test-ext2-mknod test-vmdk-flat test-vmdk-boot test-vmdk-layouts test-vhd-fixed test-vhd-ntfs-basic test-ntfs-windows test-vdi-fixed test-vdi-sparse test-vdi-sparse-scale test-vdi-sparse-torture test-diskq test-disk-output test-debian study-debian repo-audit print-config print-shell install uninstall vdisk test-ntfs-check

all: $(BIN)

vdisk:
	@$(MAKE) --no-print-directory -C $(VDISK_DIR)

$(VDISK_LIB): $(VDISK_DEPS)
	@$(MAKE) --no-print-directory -C $(VDISK_DIR)


$(BIN): $(OBJS) $(XZ_OBJS) $(VDISK_LIB) | $(BIN_DIR)
	$(CC) $(OBJS) $(XZ_OBJS) $(VDISK_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) $(INC_DIR)/version.h
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/xz_%.o: $(XZ_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	@$(MKDIR_P) "$(BUILD_DIR)"

$(BIN_DIR):
	@$(MKDIR_P) "$(BIN_DIR)"

run: $(BIN)
	@$(BIN)

test: $(BIN)
	@$(MAKE) --no-print-directory -C tests test GUPPY=../$(BIN)

test-pkg-repo-lifecycle: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-repo-lifecycle GUPPY=../$(BIN)

test-pkg-repo-gzip: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-repo-gzip GUPPY=../$(BIN)

test-pkg-plan: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-plan GUPPY=../$(BIN)

test-pkg-auto-install: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-auto-install GUPPY=../$(BIN)

test-pkg-audit: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-audit GUPPY=../$(BIN)

test-pkg-dash-preunpack: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-dash-preunpack GUPPY=../$(BIN)

test-pkg-base-passwd-preunpack: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-base-passwd-preunpack GUPPY=../$(BIN)

test-pkg-base-files-preunpack: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-base-files-preunpack GUPPY=../$(BIN)

test-pkg-libc6-configure: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-libc6-configure GUPPY=../$(BIN)

test-pkg-configure: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-configure GUPPY=../$(BIN)

test-pkg-plan-conflict: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-pkg-plan-conflict GUPPY=../$(BIN)

test-file-ops: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-file-ops GUPPY=../$(BIN)

test-ext2-mknod: $(BIN)
	@$(MAKE) --no-print-directory -C tests/ext2-mknod test GUPPY=../../$(BIN)

test-vmdk-flat: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vmdk-flat GUPPY=../$(BIN)

test-vmdk-boot: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vmdk-boot GUPPY=../$(BIN)

test-vmdk-layouts: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vmdk-layouts GUPPY=../$(BIN)

test-vhd-ntfs-basic: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vhd-ntfs-basic GUPPY=../$(BIN)

test-ntfs-windows: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-ntfs-windows GUPPY=../$(BIN)

test-ntfs-check: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-ntfs-check GUPPY=../$(BIN)

test-vhd-fixed: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vhd-fixed GUPPY=../$(BIN)

test-vdi-fixed: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vdi-fixed GUPPY=../$(BIN)

test-vdi-sparse: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vdi-sparse GUPPY=../$(BIN)

test-vdi-sparse-scale: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vdi-sparse-scale GUPPY=../$(BIN)

test-vdi-sparse-torture: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-vdi-sparse-torture GUPPY=../$(BIN)

test-diskq: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-diskq GUPPY=../$(BIN)

test-disk-output: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-disk-output GUPPY=../$(BIN)

test-debian: $(BIN)
	@$(MAKE) --no-print-directory -C tests test-debian GUPPY=../$(BIN)

study-debian: $(BIN)
	@$(MAKE) --no-print-directory -C tests study-debian GUPPY=../$(BIN) ISO="$(ISO)"

repo-audit:
	@$(PYTHON) tools/repo_audit.py

print-config:
ifeq ($(IS_WINDOWS),1)
	@echo IS_WINDOWS=$(IS_WINDOWS)  EXE=$(EXE)  NULLDEV=$(NULLDEV)
else
	@echo UNAME_S=$(UNAME_S)  EXE=$(EXE)  NULLDEV=$(NULLDEV)
endif
	@echo CC=$(CC)
	@echo PREFIX=$(PREFIX)
	@echo BINDIR=$(BINDIR)
	@echo INCLUDEDIR=$(INCLUDEDIR)
	@echo LIBDIR=$(LIBDIR)
	@echo INSTALL_DIR=$(INSTALL_DIR)
	@echo DESTDIR=$(DESTDIR)
	@echo BIN=$(BIN)
	@echo VDISK_LIB=$(VDISK_LIB)
	@echo SRCS ($(words $(SRCS)) files)
	@echo OBJS ($(words $(OBJS)) files)
	@echo SHELL=$(SHELL)

print-shell:
	@echo OS=$(OS)
	@echo ComSpec=$(ComSpec)
	@echo COMSPEC=$(COMSPEC)
	@echo SHELL=$(SHELL)
	@echo IS_WINDOWS=$(IS_WINDOWS)
	@echo NULLDEV=$(NULLDEV)
	@echo CC=$(CC)

install: all
ifeq ($(IS_WINDOWS),1)
	@if not exist "$(DESTDIR)$(BINDIR)" mkdir "$(DESTDIR)$(BINDIR)"
	@copy /Y "$(BIN_DIR)\guppy$(EXE)" "$(DESTDIR)$(BINDIR)\guppy$(EXE)" >NUL
else
	@mkdir -p "$(DESTDIR)$(BINDIR)"
	@cp -f "$(BIN)" "$(DESTDIR)$(BINDIR)/guppy$(EXE)"
endif
	@echo Installed guppy to $(BINDIR)
	@$(MAKE) --no-print-directory -C $(VDISK_DIR) install PREFIX="$(PREFIX)" INCLUDEDIR="$(INCLUDEDIR)" LIBDIR="$(LIBDIR)" DESTDIR="$(DESTDIR)"

uninstall:
	@echo "Removing $(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"
	-@$(RM_F) "$(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"
	-@$(MAKE) --no-print-directory -C $(VDISK_DIR) uninstall PREFIX="$(PREFIX)" INCLUDEDIR="$(INCLUDEDIR)" LIBDIR="$(LIBDIR)" DESTDIR="$(DESTDIR)"

clean:
	@echo Cleaning build artifacts
	-@$(RM_RF) "$(BUILD_DIR)" "$(BIN_DIR)"
	-@$(MAKE) --no-print-directory -C $(VDISK_DIR) clean
	@echo Cleaning test artifacts
	-@$(MAKE) --no-print-directory -C tests clean VERBOSE=0

version:
	guppy version

-include $(DEPS)
