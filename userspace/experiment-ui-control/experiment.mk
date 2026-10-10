SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
SAMPLE_DIR := $(patsubst %/,%,$(dir $(SAMPLE_MAKEFILE)))
PROJECT_ROOT := $(abspath $(SAMPLE_DIR)/../..)
APP_TARGET := $(notdir $(SAMPLE_DIR))
BUILD_DIR ?= $(PROJECT_ROOT)/build-$(APP_TARGET)
HOST_BUILD_DIR ?= $(PROJECT_ROOT)/build/$(APP_TARGET)-host
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
EXPERIMENT_PREKERNEL_READY ?= ON
include $(PROJECT_ROOT)/project-tools/make/common.mk
CMAKE_ARGS += -DEXPERIMENT_PREKERNEL_READY=$(EXPERIMENT_PREKERNEL_READY)
.PHONY: test
test:
	$(CMAKE) -S "$(SAMPLE_DIR)" -B "$(HOST_BUILD_DIR)"
	$(CMAKE) --build "$(HOST_BUILD_DIR)" --parallel 2
	ctest --test-dir "$(HOST_BUILD_DIR)" --output-on-failure
