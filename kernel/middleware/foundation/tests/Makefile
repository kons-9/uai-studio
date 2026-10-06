CMAKE ?= cmake
BUILD_DIR ?= $(abspath $(CURDIR)/../../../../build/middleware-tests)

.PHONY: all test

all: test

test:
	$(CMAKE) -S "$(abspath $(CURDIR)/../../tests)" -B "$(BUILD_DIR)"