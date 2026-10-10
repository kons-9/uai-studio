PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_MAKEFILE := $(abspath $(firstword $(MAKEFILE_LIST)))
SAMPLE_DIR := $(patsubst %/,%,$(dir $(SAMPLE_MAKEFILE)))
APP_TARGET := $(notdir $(SAMPLE_DIR))
BUILD_DIR ?= $(PROJECT_ROOT)/build-$(APP_TARGET)
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
UI_LAYOUT_JSON := $(SAMPLE_DIR)/config/ui_layout.json
UI_LAYOUT_HEADER := $(SAMPLE_DIR)/src/ui/ui_layout.hpp
include $(PROJECT_ROOT)/project-tools/make/common.mk
