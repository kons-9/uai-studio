PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/experiment-camera-pipe2
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= experiment-camera-pipe2
BUILD_DIR ?= $(PROJECT_ROOT)/build-experiment-camera-pipe2
SAMPLE_DEFAULT_IOC := $(PROJECT_ROOT)/userspace/experiment-camera-lcd/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0

include $(PROJECT_ROOT)/tools/common.mk
