GUPPY ?= guppy

.PHONY: test clean

test: clean
	$(GUPPY) test.script

ifeq ($(OS),Windows_NT)
clean:
	-@del /Q *.vdi 2>NUL
	-@del /Q *.log 2>NUL
else
clean:
	@rm -f -- *.vdi *.log
endif
