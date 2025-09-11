# ---- configurable ------------------------------------------------------------
CC       ?= cc
CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra
OPT      ?= -O2
CPPFLAGS ?= -Iinclude -Isrc
LDFLAGS  ?=
LDLIBS   ?=

# Install prefix (override with: make install PREFIX=/opt)
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin
DESTDIR  ?=

# Optional override: install directly to a specific directory
# e.g. make install INSTALL_DIR=/cygdrive/c/cygwin64/bin
INSTALL_DIR ?= $(BINDIR)

# Tools
INSTALL      ?= install
INSTALL_BIN  ?= $(INSTALL) -m 0755
MKDIR_P      ?= mkdir -p
STRIP        ?= strip

# ---- detect OS & set platform flags -----------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
EXE :=

ifeq ($(UNAME_S),Linux)
  CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
endif

ifeq ($(UNAME_S),Darwin)  # macOS
  CPPFLAGS += -D_DARWIN_C_SOURCE -D_FILE_OFFSET_BITS=64
endif

# Windows user (fallback if USERNAME is unset)
WINUSER := $(or $(USERNAME),$(USER))

# Cygwin
ifneq (,$(findstring CYGWIN,$(UNAME_S)))
  EXE := .exe
  CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
  # Default to global Cygwin bin unless user already set INSTALL_DIR on CLI
  ifneq ($(origin INSTALL_DIR), command line)
    INSTALL_DIR := /cygdrive/c/cygwin64/bin
  endif
endif

# MinGW / MSYS
ifneq (,$(findstring MINGW,$(UNAME_S)))
  EXE := .exe
  CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D__USE_MINGW_ANSI_STDIO=1
  # Sensible default for MinGW (user bin); override as needed
  ifneq ($(origin INSTALL_DIR), command line)
    INSTALL_DIR := /c/Users/$(WINUSER)/bin
  endif
endif

# ---- dirs & files ------------------------------------------------------------
SRC_DIR   := src
INC_DIR   := include
BUILD_DIR := build
BIN_DIR   := bin
BIN       := $(BIN_DIR)/guppy$(EXE)

# All C files in src/
SRCS   := $(wildcard $(SRC_DIR)/*.c)
OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS   := $(OBJS:.o=.d)

# ---- rules -------------------------------------------------------------------
.PHONY: all clean distclean run print-config install install-strip uninstall

all: $(BIN)

# Final link
$(BIN): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

# Compile each .c -> build/.o with dep files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR) $(INC_DIR)/version.h
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Auto-generate a simple version header (from git describe, or fallback)
$(INC_DIR)/version.h:
	@mkdir -p $(INC_DIR)
	@echo '/* auto-generated; do not edit */' > $(INC_DIR)/version.h
	@echo '#pragma once' >> $(INC_DIR)/version.h
	@echo -n '#define GUPPY_VERSION "' >> $(INC_DIR)/version.h
	@{ git describe --tags --always --dirty 2>/dev/null || echo 0.0.0; } >> $(INC_DIR)/version.h
	@echo '"' >> $(INC_DIR)/version.h

# Create build/bin directories if missing
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Convenience: run the REPL
run: $(BIN)
	@$(BIN)

# Show current config
print-config:
	@echo "UNAME_S=$(UNAME_S)  EXE=$(EXE)"
	@echo "PREFIX=$(PREFIX)"
	@echo "BINDIR=$(BINDIR)"
	@echo "INSTALL_DIR=$(INSTALL_DIR)"
	@echo "BIN=$(BIN)"
	@echo "SRCS ($(words $(SRCS)) files)"
	@echo "OBJS ($(words $(OBJS)) files)"

# Install / Uninstall ----------------------------------------------------------
# Use DESTDIR for packaging (e.g., DESTDIR=$(PWD)/pkgroot make install)
install: $(BIN)
	$(MKDIR_P) "$(DESTDIR)$(INSTALL_DIR)"
	$(INSTALL_BIN) "$(BIN)" "$(DESTDIR)$(INSTALL_DIR)/"

install-strip: install
	-$(STRIP) "$(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)" || true

uninstall:
	@echo "Removing $(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"
	@rm -f "$(DESTDIR)$(INSTALL_DIR)/guppy$(EXE)"

# Clean ------------------------------------------------------------------------
clean:
	@rm -f $(BUILD_DIR)/*.o $(BUILD_DIR)/*.d $(BIN)
	@rmdir $(BUILD_DIR) 2>/dev/null || true
	@rmdir $(BIN_DIR) 2>/dev/null || true

# Distclean also removes generated version header
distclean: clean
	@rm -f $(INC_DIR)/version.h

# Include auto-generated dependency files
-include $(DEPS)
