CSTD     ?= -std=c11
WARN     ?= -Wall -Wextra
OPT      ?= -O2
CPPFLAGS ?= -Iinclude
AR       ?= ar
ARFLAGS  ?= rcs

ifeq ($(OS),Windows_NT)
  CC := gcc
  MKDIR_P := mkdir
else
  CC ?= cc
  MKDIR_P := mkdir -p
endif


ifeq ($(OS),Windows_NT)
  PREFIX ?= $(USERPROFILE)/.local
else
  PREFIX ?= /usr/local
endif

INCLUDEDIR ?= $(PREFIX)/include
LIBDIR     ?= $(PREFIX)/lib
DESTDIR    ?=
SRC_DIR   := src
BUILD_DIR := build
LIB        := libvdisk.a
SRCS       := $(wildcard $(SRC_DIR)/*.c)
OBJS       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS       := $(OBJS:.o=.d)

.PHONY: all clean install uninstall
all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	@$(MKDIR_P) "$(BUILD_DIR)"

install: $(LIB)
ifeq ($(OS),Windows_NT)
	@if not exist "$(DESTDIR)$(INCLUDEDIR)" mkdir "$(DESTDIR)$(INCLUDEDIR)"
	@if not exist "$(DESTDIR)$(LIBDIR)" mkdir "$(DESTDIR)$(LIBDIR)"
	@copy /Y "include\vdisk.h" "$(DESTDIR)$(INCLUDEDIR)\vdisk.h" >NUL
	@copy /Y "$(LIB)" "$(DESTDIR)$(LIBDIR)\$(LIB)" >NUL
else
	@mkdir -p "$(DESTDIR)$(INCLUDEDIR)" "$(DESTDIR)$(LIBDIR)"
	@cp -f "include/vdisk.h" "$(DESTDIR)$(INCLUDEDIR)/vdisk.h"
	@cp -f "$(LIB)" "$(DESTDIR)$(LIBDIR)/$(LIB)"
endif
	@echo Installed vdisk.h to $(INCLUDEDIR)
	@echo Installed $(LIB) to $(LIBDIR)

uninstall:
ifeq ($(OS),Windows_NT)
	-@del /Q "$(DESTDIR)$(INCLUDEDIR)\vdisk.h" 2>NUL
	-@del /Q "$(DESTDIR)$(LIBDIR)\$(LIB)" 2>NUL
else
	-@rm -f "$(DESTDIR)$(INCLUDEDIR)/vdisk.h" "$(DESTDIR)$(LIBDIR)/$(LIB)"
endif

clean:
	-@rm -rf "$(BUILD_DIR)" "$(LIB)"

-include $(DEPS)
