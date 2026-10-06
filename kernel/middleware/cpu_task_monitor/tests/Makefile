CMAKE ?= cmake
CTEST ?= ctest
HOST_TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../tests)
BUILD_DIR ?= $(abspath $(HOST_TEST_DIR)/../../../build/middleware-tests)
TESTS := cpu_task_monitor_test
TEST_REGEX := ^($(subst $() ,|,$(strip $(TESTS))))$$

.PHONY: all configure build test

all: test

configure:
	$(CMAKE) -S "$(HOST_TEST_DIR)" -B "$(BUILD_DIR)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(addprefix --target ,$(TESTS))

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure -R '$(TEST_REGEX)'