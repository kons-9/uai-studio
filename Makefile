.DEFAULT_GOAL := help

ENV_FILE ?= env.mk
-include $(ENV_FILE)

APP_TARGET ?= sample-hello-world
BUILD_DIR ?= build
CMAKE ?= cmake

# Host and board settings are read here and exported to CMake.  The Makefile
# intentionally contains only the command-line convenience layer.
CONFIG_FILE ?= config/local.mk
-include $(CONFIG_FILE)

CUBEMX_IOC ?= userspace/$(APP_TARGET)/config/stm32n6570-dk-fullsecure.ioc
# sample-camera-pipe2 uses the sample-camera-lcd CubeMX peripheral/startup project and configures
# the second DCMIPP pipe in the application.
ifneq (,$(filter sample-camera-pipe2 sample-camera-pipe2-lab,$(APP_TARGET)))
ifneq ($(filter userspace/sample-ai/config/stm32n6570-dk-sample-ai.ioc,$(CUBEMX_IOC)),)
CUBEMX_IOC := userspace/sample-camera-lcd/config/stm32n6570-dk-fullsecure.ioc
endif
ifeq ($(origin CUBEMX_IOC),file)
ifeq ($(wildcard $(CUBEMX_IOC)),)
CUBEMX_IOC := userspace/sample-camera-lcd/config/stm32n6570-dk-fullsecure.ioc
endif
endif
endif
CUBEMX_OUTPUT_DIR ?= $(BUILD_DIR)/cubemx
STEDGEAI_LIB_DIR ?=
AI_VISION_MODELS_PP_DIR ?=
UART_DEVICE ?= auto
UART_BAUD ?= 115200

# Keep environment-specific values in config/local.mk while making them
# visible to CMake.  CMake owns defaults, discovery, validation, and tool
# command construction.
export ARM_NONE_EABI_TOOLCHAIN_PATH
export STM32CUBE_N6_DIR
export CUBEMX_EXECUTABLE CUBEMX_IOC CUBEMX_OUTPUT_DIR
export STEDGEAI_LIB_DIR AI_VISION_MODELS_PP_DIR
export STM32_SIGNING_TOOL_CLI STM32_SIGN_INPUT STM32_SIGN_OUTPUT STM32_SIGNING_ARGS
export STM32_PROGRAMMER_ROOT STM32_PROGRAMMER_CLI STM32_PROGRAMMER_LIB
export STM32_EXTERNAL_LOADER STM32_PROGRAM_IMAGE STM32_PROGRAM_ADDRESS
export STM32_PROGRAM_PORT STM32_PROGRAM_EXTRA_ARGS STM32_PROGRAM_SERIAL
export STM32_RAM_IMAGE STM32_RAM_ADDRESS STM32_RAM_ENTRY STM32_RAM_STACK STM32_RAM_XPSR
export UART_DEVICE UART_BAUD

CMAKE_ARGS := -S . -B "$(BUILD_DIR)" -DAPP_TARGET="$(APP_TARGET)"
ifneq ($(strip $(STM32CUBE_N6_DIR)),)
CMAKE_ARGS += -DSTM32CUBE_N6_DIR="$(STM32CUBE_N6_DIR)"
endif
ifneq ($(strip $(STEDGEAI_LIB_DIR)),)
CMAKE_ARGS += -DSTEDGEAI_LIB_DIR="$(STEDGEAI_LIB_DIR)"
endif
ifneq ($(strip $(AI_VISION_MODELS_PP_DIR)),)
CMAKE_ARGS += -DAI_VISION_MODELS_PP_DIR="$(AI_VISION_MODELS_PP_DIR)"
endif

.PHONY: help configure generate cubemx-generate sample-ai-deps build attach monitor ram-run sign program flash run clean

help:
	@echo "make configure  - Configure CMake"
	@echo "make generate   - Generate STM32Cube sources from the project IOC using CubeMX CLI"
	@echo "make sample-ai-deps - Check sample-ai STEdgeAI/post-processing dependencies"
	@echo "make build      - Build $(APP_TARGET)"
	@echo "make attach     - Check the native Linux ST-LINK USB connection"
	@echo "make monitor    - Open the configured UART monitor"
	@echo "make ram-run    - Build and load $(APP_TARGET) with STM32CubeProgrammer"
	@echo "make sign       - Create an STM32N6 signed image"
	@echo "make program    - Write an external-flash image with STM32CubeProgrammer"
	@echo "make flash      - Alias for make program"
	@echo "make clean      - Clean the CMake build tree"
	@echo
	@echo "Set board/tool paths in config/local.mk (see config/local.mk.example)."

configure:
	$(CMAKE) $(CMAKE_ARGS)

generate: cubemx-generate

cubemx-generate: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target cubemx-generate

sample-ai-deps:
	$(if $(strip $(STEDGEAI_LIB_DIR)),sh userspace/sample-ai/scripts/setup_third_party.sh "$(STEDGEAI_LIB_DIR)",sh userspace/sample-ai/scripts/setup_third_party.sh)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target $(APP_TARGET)

attach:
	./usb_attach.sh

monitor:
	sh tools/uart-monitor.sh

ram-run: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target ram-run

sign: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target stm32-sign

program: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target program

flash: program

run: ram-run

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean
