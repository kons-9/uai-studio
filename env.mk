# Default native-Linux build settings for the STM32N6570-DK sample-ai image.
# Command-line assignments still override these settings.
APP_TARGET ?= sample-ai
CUBEMX_IOC ?= userspace/sample-ai/config/stm32n6570-dk-sample-ai.ioc
CUBEMX_OUTPUT_DIR ?= build/cubemx
