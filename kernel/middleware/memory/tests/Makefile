CMAKE ?= cmake
CTEST ?= ctest
HOST_TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../tests)
BUILD_DIR ?= $(abspath $(HOST_TEST_DIR)/../../../build/middleware-tests)
TSAN_BUILD_DIR ?= $(abspath $(HOST_TEST_DIR)/../../../build/middleware-tests-tsan)
TESTS := static_memory_layout_test static_memory_layout_resolve_test
TEST_REGEX := ^($(subst $() ,|,$(strip $(TESTS))))$$

.PHONY: all configure build test tsan

all: test

configure:
	$(CMAKE) -S "$(HOST_TEST_DIR)" -B "$(BUILD_DIR)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(addprefix --target ,$(TESTS))

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure -R '$(TEST_REGEX)'

tsan:
	$(CMAKE) -S "$(HOST_TEST_DIR)" -B "$(TSAN_BUILD_DIR)" -DUAI_TEST_TSAN=ON
	$(CMAKE) --build "$(TSAN_BUILD_DIR)" $(addprefix --target ,$(TESTS))
	$(CTEST) --test-dir "$(TSAN_BUILD_DIR)" --output-on-failure -R '$(TEST_REGEX)'
