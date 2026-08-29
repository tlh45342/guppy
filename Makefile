# -----------------------------------------------------------------------------
# Makefile - guppy
#
# Goals:
#  - Works on POSIX (Linux/macOS) and Windows (when GNU make uses sh.exe)
#  - No /dev/null on Windows (use NUL)
#  - Defaults to gcc on Windows (cc often missing)
#  - "make clean" should not print the Windows "path not found" noise
# -----------------------------------------------------------------------------

# ---- configurable ------------------------------------------------------------
CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra
OPT      ?= -O2
CPPFLAGS ?= -Iinclude -Isrc
LDFLAGS  ?=
LDLIBS   ?=


# MinGW: enable C99 printf formats like %zu / %zd (fixes DBG format warnings)
ifeq ($(OS),Windows_NT)
  CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
endif


# Tools (assumes sh.exe + coreutils-ish environment on Windows)
ifeq ($(OS),Windows_NT)
  MKDIR_P := mkdir
else
  MKDIR_P := mkdir -p
endif
RM_RF    ?= rm -rf
RM_F     ?= rm -f
CP       ?= cp -f
STRIP    ?= strip

# ---- platform detect ---------------------------------------------------------
IS_WINDOWS := 0
EXE :=
NULLDEV := /dev/null

ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
  EXE := .exe
  NULLDEV := NUL
  # Windows: default to gcc (user can override on command line)
  CC := gcc
else
  # Non-Windows: default to cc
  CC := cc
endif

# Install prefix (override: make PREFIX="C:/Something" install)
ifeq ($(OS),Windows_NT)
  PREFIX ?= $(USERPROFILE)/.local
else
  PREFIX ?= /usr/local
endif

BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=

# Optional override if you want install somewhere else directly
# e.g. make install INSTALL_DIR=/c/Users/You/bin
INSTALL_DIR ?= $(BINDIR)

# Only do uname-based tweaks when NOT native Windows.
# (Important: GNU make parses the whole file even for "make clean",
#  so don't run uname with /dev/null during parse on Windows.)
ifneq ($(IS_WINDOWS),1)
  UNAME_S := $(shell uname -s 2>$(NULLDEV) || echo Unknown)

  ifeq ($(UNAME_S),Linux)
    CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
  endif

  ifeq ($(UNAME_S),Darwin)
    CPPFLAGS += -D_DARWIN_C_SOURCE -D_FILE_OFFSET_BITS=64
  endif

  # Optional: MinGW ANSI stdio formatting
  ifneq (,$(findstring MINGW,$(UNAME_S)))
    CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
  endif
endif

# ---- dirs & files ------------------------------------------------------------
SRC_DIR   := src
INC_DIR   := include
BUILD_DIR := build
BIN_DIR   := bin

BIN := $(BIN_DIR)/guppy$(EXE)

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# ---- rules -------------------------------------------------------------------
.PHONY: all clean distclean run print-config print-shell install uninstall

all: $(BIN)

# Link
$(BIN): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

# Compile (with depfiles)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) $(INC_DIR)/version.h
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Dirs
$(BUILD_DIR):
	@$(MKDIR_P) "$(BUILD_DIR)"

$(BIN_DIR):
	@$(MKDIR_P) "$(BIN_DIR)"

# Convenience run
run: $(BIN)
	@$(BIN)

# Show current config (your pattern, adapted to this project)
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
	@echo SRCS ($(words $(SRCS)) files)
	@echo OBJS ($(words $(OBJS)) files)
	@echo SHELL=$(SHELL)

# Handy environment peek
print-shell:
	@echo OS=$(OS)
	@echo ComSpec=$(ComSpec)
	@echo COMSPEC=$(COMSPEC)
	@echo SHELL=$(SHELL)
	@echo IS_WINDOWS=$(IS_WINDOWS)
	@echo NULLDEV=$(NULLDEV)
	@echo CC=$(CC)

# --- install ---
install: all
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	@copy /Y "$(BIN_DIR)\guppy$(EXE)" "$(BINDIR)\guppy$(EXE)" >NUL
	@echo Installed guppy to $(BINDIR)

uninstall:
	@echo "Removing $(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"
	-@$(RM_F) "$(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"

# Clean ------------------------------------------------------------------------
clean:
	@echo Cleaning build artifacts
	-@$(RM_RF) "$(BUILD_DIR)" "$(BIN_DIR)"

# --- version ---
version:
	guppy version

# Include depfiles (safe if missing)
-include $(DEPS)
