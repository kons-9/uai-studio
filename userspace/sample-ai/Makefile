PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/sample-ai
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= sample-ai
BUILD_DIR ?= $(PROJECT_ROOT)/build-sample-ai
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-sample-ai.ioc
ENABLE_AI := 1
ENABLE_THREAD_MONITOR := 1
ENABLE_CPU_TASK_MONITOR := 0

include $(PROJECT_ROOT)/userspace/make/common.mk
