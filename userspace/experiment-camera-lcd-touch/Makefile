PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/experiment-camera-lcd-touch
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= experiment-camera-lcd-touch
BUILD_DIR ?= $(PROJECT_ROOT)/build-experiment-camera-lcd-touch
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0

# Use this image's Reset_Handler and linker stack. The host-wide settings target
# the AI application's cache-sanitizing RAM entry point instead.
STM32_RAM_ENTRY ?= 0x34000800
STM32_RAM_STACK ?= 0x34030000

include $(PROJECT_ROOT)/build-system/make/common.mk
