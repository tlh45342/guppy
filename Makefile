# Compiler / flags
CC       ?= gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
CPPFLAGS ?=
LDFLAGS  ?=

# Install path (your current setting)
INSTALL_DIR ?= /cygdrive/c/cygwin64/bin

# Output dirs / target
BINDIR   ?= bin
BUILDDIR ?= build
TARGET   ?= $(BINDIR)/guppy.exe

# ---- Source discovery ----
# If you have a src/ layout, use that. Otherwise fall back to root guppy.c.
ifneq ($(wildcard src/*.c),)
  SRCDIR   := src
  INCDIR   := include
  SOURCES  := $(wildcard $(SRCDIR)/*.c)
  CPPFLAGS += -I$(SRCDIR) -I$(INCDIR)
else
  SRCDIR   := .
  SOURCES  := guppy.c
endif

OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

# Tools
MKDIR_P := mkdir -p
RM      := rm -f
CP      := cp
INSTALL := install

# Default
.PHONY: all
all: $(TARGET)

# Link
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# Compile
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Dirs
$(BUILDDIR):
	$(MKDIR_P) $(BUILDDIR)

$(BINDIR):
	$(MKDIR_P) $(BINDIR)

# Convenience
.PHONY: clean distclean run debug release install

clean:
	$(RM) $(OBJECTS) $(TARGET)

distclean: clean
	-$(RM) -r $(BUILDDIR) $(BINDIR)

# Quick run (if your app has no args)
run: $(TARGET)
	$(TARGET)

# Build types
debug: CFLAGS := -Wall -Wextra -Og -g -std=c11
debug: clean all

release: CFLAGS := -Wall -Wextra -O2 -DNDEBUG -std=c11
release: clean all

# Install (needs admin rights if INSTALL_DIR is system-wide)
install: $(TARGET)
	$(INSTALL) -m 755 $(TARGET) $(INSTALL_DIR)