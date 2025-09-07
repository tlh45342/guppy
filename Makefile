# ---- configurable ------------------------------------------------------------
CC       ?= cc
CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra
OPT      ?= -O2
CPPFLAGS ?= -Iinclude -Isrc
LDFLAGS  ?=
LDLIBS   ?=

# ---- detect OS & set platform flags -----------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)

# default: no .exe suffix
EXE :=

ifeq ($(UNAME_S),Linux)
  CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
endif

ifeq ($(UNAME_S),Darwin)  # macOS
  CPPFLAGS += -D_DARWIN_C_SOURCE -D_FILE_OFFSET_BITS=64
endif

# Cygwin
ifneq (,$(findstring CYGWIN,$(UNAME_S)))
  EXE := .exe
  CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
endif

# MinGW (Git Bash, MSYS2, etc.)
ifneq (,$(findstring MINGW,$(UNAME_S)))
  EXE := .exe
  CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D__USE_MINGW_ANSI_STDIO=1
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
.PHONY: all clean run print-os

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

# Show what OS the Makefile detected
print-os:
	@echo "UNAME_S=$(UNAME_S)  EXE=$(EXE)"

# Clean
clean:
	@rm -f $(BUILD_DIR)/*.o $(BUILD_DIR)/*.d $(BIN)
	@rmdir $(BUILD_DIR) 2>/dev/null || true
	@rmdir $(BIN_DIR) 2>/dev/null || true

# Include auto-generated dependency files
-include $(DEPS)
