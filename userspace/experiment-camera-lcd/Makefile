PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/experiment-camera-lcd
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= experiment-camera-lcd
BUILD_DIR ?= $(PROJECT_ROOT)/build-experiment-camera-lcd
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0

include $(PROJECT_ROOT)/build-system/make/common.mk
