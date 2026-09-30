PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/ai-app
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= ai-app
BUILD_DIR ?= $(PROJECT_ROOT)/build-ai-app-person
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-ai-app.ioc
ENABLE_AI := 1
ENABLE_THREAD_MONITOR := 1
ENABLE_CPU_TASK_MONITOR := 1
CUBEMX_GENERATOR := script

include $(PROJECT_ROOT)/tools/common.mk
