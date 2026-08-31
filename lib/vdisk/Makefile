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

SRC_DIR   := src
BUILD_DIR := build
LIB        := libvdisk.a
SRCS       := $(wildcard $(SRC_DIR)/*.c)
OBJS       := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS       := $(OBJS:.o=.d)

.PHONY: all clean
all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	@$(MKDIR_P) "$(BUILD_DIR)"

clean:
	-@rm -rf "$(BUILD_DIR)" "$(LIB)"

-include $(DEPS)
