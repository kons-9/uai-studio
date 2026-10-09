PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_MAKEFILE := $(abspath $(firstword $(MAKEFILE_LIST)))
SAMPLE_DIR := $(patsubst %/,%,$(dir $(SAMPLE_MAKEFILE)))
APP_TARGET := $(notdir $(SAMPLE_DIR))
BUILD_DIR ?= $(PROJECT_ROOT)/build-$(APP_TARGET)
SAMPLE_DEFAULT_IOC := $(PROJECT_ROOT)/userspace/experiment-camera-lcd/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
include $(PROJECT_ROOT)/build-system/make/common.mk