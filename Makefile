# -----------------------------------------------------------------------------
# Makefile - guppy
# -----------------------------------------------------------------------------
CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra
OPT      ?= -O2
CPPFLAGS ?= -Iinclude -Isrc -Ilib/vdisk/include
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
else
  CC := cc
endif

ifeq ($(OS),Windows_NT)
  PREFIX ?= $(USERPROFILE)/.local
else
  PREFIX ?= /usr/local
endif

BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=
INSTALL_DIR ?= $(BINDIR)

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

BIN := $(BIN_DIR)/guppy$(EXE)
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean distclean run print-config print-shell install uninstall vdisk

all: $(BIN)

vdisk:
	$(MAKE) -C $(VDISK_DIR)

$(VDISK_LIB):
	$(MAKE) -C $(VDISK_DIR)

$(BIN): $(OBJS) $(VDISK_LIB) | $(BIN_DIR)
	$(CC) $(OBJS) $(VDISK_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) $(INC_DIR)/version.h
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	@$(MKDIR_P) "$(BUILD_DIR)"

$(BIN_DIR):
	@$(MKDIR_P) "$(BIN_DIR)"

run: $(BIN)
	@$(BIN)

print-config:
ifeq ($(IS_WINDOWS),1)
	@echo IS_WINDOWS=$(IS_WINDOWS)  EXE=$(EXE)  NULLDEV=$(NULLDEV)
else
	@echo UNAME_S=$(UNAME_S)  EXE=$(EXE)  NULLDEV=$(NULLDEV)
endif
	@echo CC=$(CC)
	@echo PREFIX=$(PREFIX)
	@echo BINDIR=$(BINDIR)
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
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	@copy /Y "$(BIN_DIR)\guppy$(EXE)" "$(BINDIR)\guppy$(EXE)" >NUL
	@echo Installed guppy to $(BINDIR)

uninstall:
	@echo "Removing $(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"
	-@$(RM_F) "$(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"

clean:
	@echo Cleaning build artifacts
	-@$(RM_RF) "$(BUILD_DIR)" "$(BIN_DIR)"
	-@$(MAKE) -C $(VDISK_DIR) clean

version:
	guppy version

-include $(DEPS)
