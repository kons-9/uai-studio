PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/sample-camera-lcd
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= sample-camera-lcd
BUILD_DIR ?= $(PROJECT_ROOT)/build-sample-camera-lcd
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0

include $(PROJECT_ROOT)/userspace/make/common.mk
