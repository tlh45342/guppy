GUPPY ?= guppy

.PHONY: all test clean

all: test

test: clean
	$(GUPPY) iso-test1.script
	$(GUPPY) test2.script
	@echo ISO regression PASS
	@echo Log: iso-regression.log

clean:
	-@rm -f iso-regression.log
