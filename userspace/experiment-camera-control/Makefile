PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/experiment-camera-control
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET := experiment-camera-control
BUILD_DIR ?= $(PROJECT_ROOT)/build-experiment-camera-control
HOST_BUILD_DIR ?= $(PROJECT_ROOT)/build/experiment-camera-control-host
SAMPLE_DEFAULT_IOC := $(PROJECT_ROOT)/userspace/experiment-camera-lcd/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
EXPERIMENT_PREKERNEL_READY ?= ON

include $(PROJECT_ROOT)/build-system/make/common.mk

CMAKE_ARGS += -DEXPERIMENT_PREKERNEL_READY=$(EXPERIMENT_PREKERNEL_READY)

.PHONY: test
test:
	$(CMAKE) -S "$(SAMPLE_DIR)" -B "$(HOST_BUILD_DIR)"
	$(CMAKE) --build "$(HOST_BUILD_DIR)" --parallel 2
	ctest --test-dir "$(HOST_BUILD_DIR)" --output-on-failure