HOST_TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
HOST_TEST_ROOT := $(abspath $(HOST_TEST_DIR)/../../..)
CMAKE ?= cmake
CTEST ?= ctest
BUILD_DIR ?= $(HOST_TEST_ROOT)/build/middleware-tests
TSAN_BUILD_DIR ?= $(HOST_TEST_ROOT)/build/middleware-tests-tsan

.PHONY: all configure build test tsan

all: test

configure:
	$(CMAKE) -S "$(HOST_TEST_DIR)" -B "$(BUILD_DIR)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target "$(TEST_NAME)"

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure -R '^$(TEST_NAME)$$'

tsan:
	$(CMAKE) -S "$(HOST_TEST_DIR)" -B "$(TSAN_BUILD_DIR)" -DUAI_TEST_TSAN=ON
	$(CMAKE) --build "$(TSAN_BUILD_DIR)" --target "$(TEST_NAME)"
	$(CTEST) --test-dir "$(TSAN_BUILD_DIR)" --output-on-failure -R '^$(TEST_NAME)$$'