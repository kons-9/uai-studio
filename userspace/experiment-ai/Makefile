PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/experiment-ai
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= experiment-ai
BUILD_DIR ?= $(PROJECT_ROOT)/build-experiment-ai
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-experiment-ai.ioc
ENABLE_AI := 1
ENABLE_THREAD_MONITOR := 1
ENABLE_CPU_TASK_MONITOR := 0

include $(PROJECT_ROOT)/build-system/make/common.mk
