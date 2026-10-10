PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/mini-ai-app
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= mini-ai-app
BUILD_DIR ?= $(PROJECT_ROOT)/build-mini-ai-app
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-mini-ai-app.ioc
ENABLE_AI := 1
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 1
# The person model, its generator, and the post-processing sources are shared
# with ai-app so both applications run the identical network image.
AI_MODELS_DIR := $(PROJECT_ROOT)/userspace/ai-app/models
AI_MODEL_NAMES := person

include $(PROJECT_ROOT)/project-tools/make/common.mk
