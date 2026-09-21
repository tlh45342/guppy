# Local generated-artifact cleanup for this test directory.

.PHONY: clean

ifeq ($(OS),Windows_NT)
clean:
	-@del /Q *.vdi 2>NUL
	-@del /Q *.log 2>NUL
else
clean:
	@rm -f -- *.vdi *.log
endif
