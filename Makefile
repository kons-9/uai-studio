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

ifeq ($(APP_TARGET),sample-ai)
CUBEMX_IOC ?= userspace/sample-ai/config/stm32n6570-dk-sample-ai.ioc
else
CUBEMX_IOC ?= userspace/$(APP_TARGET)/config/stm32n6570-dk-fullsecure.ioc
endif
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

# ThreadMonitor acquisition settings for sample-ai.
THREAD_MONITOR_ELF ?= $(BUILD_DIR)/userspace/sample-ai/sample-ai.elf
THREAD_MONITOR_DUMP ?= $(BUILD_DIR)/thread_monitor.bin
THREAD_MONITOR_JSON ?= $(BUILD_DIR)/thread_monitor.json
THREAD_MONITOR_PNG ?= $(BUILD_DIR)/thread_monitor.png
THREAD_MONITOR_CPU_HZ ?= 600000000
THREAD_MONITOR_NM ?= arm-none-eabi-nm
THREAD_MONITOR_PYTHON ?= python3
THREAD_MONITOR_UV ?= uv
THREAD_MONITOR_CONNECTION ?= $(AI_PROGRAM_CONNECTION) mode=HOTPLUG
THREAD_MONITOR_LD_PRELOAD ?= $(firstword $(wildcard \
	/lib/x86_64-linux-gnu/libstdc++.so.6 \
	/usr/lib/x86_64-linux-gnu/libstdc++.so.6))

# sample-ai model generation and external-flash programming settings.
STEDGEAI_BIN ?= /opt/ST/STEdgeAI/4.0/Utilities/linux
AI_MODELS_DIR := userspace/sample-ai/models
AI_MODEL_GENERATOR := $(AI_MODELS_DIR)/generate_model.sh
AI_BUILD_DIR := $(BUILD_DIR)/userspace/sample-ai
AI_PERSON_WEIGHTS := $(AI_MODELS_DIR)/person/network_data.hex
AI_SEGMENTATION_WEIGHTS := $(AI_MODELS_DIR)/segmentation/network_data.hex
AI_FACE_WEIGHTS := $(AI_MODELS_DIR)/face/network_data.hex
AI_PERSON_BLOB := $(AI_BUILD_DIR)/network_blobs_person.hex
AI_SEGMENTATION_BLOB := $(AI_BUILD_DIR)/network_blobs_segmentation.hex
AI_FACE_BLOB := $(AI_BUILD_DIR)/network_blobs_face.hex

AI_MODEL_OPTIMIZATION ?= balanced
AI_MODEL_INPUT_DATA_TYPE ?= uint8
AI_MODEL_OUTPUT_DATA_TYPE ?= int8
AI_MODEL_INPUTS_CH_POSITION ?= chlast
AI_MODEL_OUTPUTS_CH_POSITION ?=
AI_MODEL_C_API ?= st-ai
AI_MODEL_CUT_OUTPUT_TENSORS ?=
AI_MODEL_NETWORK_ADDRESS ?=
AI_MODEL_DOWNLOAD_URL ?=

ifneq ($(strip $(STM32_PROGRAMMER_ROOT)),)
STM32_PROGRAMMER_CLI ?= $(STM32_PROGRAMMER_ROOT)/bin/STM32_Programmer_CLI
STM32_EXTERNAL_LOADER ?= $(STM32_PROGRAMMER_ROOT)/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr
endif
AI_PROGRAM_CONNECTION := port=$(STM32_PROGRAM_PORT)$(if $(strip $(STM32_PROGRAM_SERIAL)), sn=$(STM32_PROGRAM_SERIAL))

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
export STEDGEAI_BIN
export AI_MODEL_OPTIMIZATION AI_MODEL_INPUT_DATA_TYPE AI_MODEL_OUTPUT_DATA_TYPE
export AI_MODEL_INPUTS_CH_POSITION AI_MODEL_OUTPUTS_CH_POSITION AI_MODEL_C_API
export AI_MODEL_CUT_OUTPUT_TENSORS AI_MODEL_NETWORK_ADDRESS AI_MODEL_DOWNLOAD_URL

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

.PHONY: help setup init configure generate cubemx-generate sample-ai-deps \
	ai-model-person ai-model-segmentation ai-model-face ai-models ai-build \
	ai-load-weights ai-load-blobs ai-load ai-init ai-run build attach monitor \
	ram-run ram-load thread-monitor-dump thread-monitor sign program flash run clean

help:
	@echo "make configure  - Configure CMake"
	@echo "make generate   - Generate STM32Cube sources from the project IOC using CubeMX CLI"
	@echo "make sample-ai-deps - Check sample-ai STEdgeAI/post-processing dependencies"
	@echo "make setup      - Prepare dependencies, models, CubeMX sources, and CMake"
	@echo "make ai-models  - Download and generate person/segmentation/face models"
	@echo "make ai-build   - Generate AI models and build sample-ai"
	@echo "make build      - Build $(APP_TARGET)"
	@echo "make ai-load    - Write AI weights and command blobs to external Flash"
	@echo "make ai-init    - Alias for make ai-load"
	@echo "make attach     - Check the native Linux ST-LINK USB connection"
	@echo "make monitor    - Open the configured UART monitor"
	@echo "make thread-monitor-dump - Dump sample-ai ThreadMonitor from the board"
	@echo "make thread-monitor - Dump, decode, and analyze ThreadMonitor"
	@echo "make ram-run    - Build and load $(APP_TARGET) with STM32CubeProgrammer"
	@echo "make ram-load   - Alias for make ram-run"
	@echo "make ai-run     - Write AI data, then load and run sample-ai in RAM"
	@echo "make sign       - Create an STM32N6 signed image"
	@echo "make program    - Write an external-flash image with STM32CubeProgrammer"
	@echo "make flash      - Alias for make program"
	@echo "make clean      - Clean the CMake build tree"
	@echo
	@echo "Set board/tool paths in config/local.mk (see config/local.mk.example)."

setup:
ifeq ($(APP_TARGET),sample-ai)
	+$(MAKE) sample-ai-deps
	+$(MAKE) ai-models
endif
	+$(MAKE) configure
	+$(MAKE) cubemx-generate

init: setup

configure:
	$(CMAKE) $(CMAKE_ARGS)

generate: cubemx-generate

cubemx-generate: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target cubemx-generate

sample-ai-deps:
	$(if $(strip $(STEDGEAI_LIB_DIR)),sh userspace/sample-ai/scripts/setup_third_party.sh "$(STEDGEAI_LIB_DIR)",sh userspace/sample-ai/scripts/setup_third_party.sh)

ai-model-person:
	PATH="$(STEDGEAI_BIN):$(PATH)" sh "$(AI_MODEL_GENERATOR)" person

ai-model-segmentation:
	PATH="$(STEDGEAI_BIN):$(PATH)" sh "$(AI_MODEL_GENERATOR)" segmentation

ai-model-face:
	PATH="$(STEDGEAI_BIN):$(PATH)" sh "$(AI_MODEL_GENERATOR)" face

ai-models: ai-model-person ai-model-segmentation ai-model-face

ai-build: ai-models
	+$(MAKE) build

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target $(APP_TARGET)

ai-load-weights:
	@test -x "$(STM32_PROGRAMMER_CLI)" || { echo "STM32_PROGRAMMER_CLI is not executable: $(STM32_PROGRAMMER_CLI)" >&2; exit 2; }
	@test -f "$(STM32_EXTERNAL_LOADER)" || { echo "STM32_EXTERNAL_LOADER is missing: $(STM32_EXTERNAL_LOADER)" >&2; exit 2; }
	@set -eu; \
	for image in \
		"$(AI_PERSON_WEIGHTS)" \
		"$(AI_SEGMENTATION_WEIGHTS)" \
		"$(AI_FACE_WEIGHTS)"; do \
		test -f "$$image" || { echo "AI weight image is missing: $$image" >&2; exit 2; }; \
		echo "Programming AI weights: $$image"; \
		"$(STM32_PROGRAMMER_CLI)" -c "$(AI_PROGRAM_CONNECTION)" \
			-el "$(STM32_EXTERNAL_LOADER)" -w "$$image" -v \
			$(STM32_PROGRAM_EXTRA_ARGS); \
	done

ai-load-blobs: build
	@test -x "$(STM32_PROGRAMMER_CLI)" || { echo "STM32_PROGRAMMER_CLI is not executable: $(STM32_PROGRAMMER_CLI)" >&2; exit 2; }
	@test -f "$(STM32_EXTERNAL_LOADER)" || { echo "STM32_EXTERNAL_LOADER is missing: $(STM32_EXTERNAL_LOADER)" >&2; exit 2; }
	@set -eu; \
	for image in \
		"$(AI_PERSON_BLOB)" \
		"$(AI_SEGMENTATION_BLOB)" \
		"$(AI_FACE_BLOB)"; do \
		test -f "$$image" || { echo "AI command blob is missing: $$image" >&2; exit 2; }; \
		echo "Programming AI command blob: $$image"; \
		"$(STM32_PROGRAMMER_CLI)" -c "$(AI_PROGRAM_CONNECTION)" \
			-el "$(STM32_EXTERNAL_LOADER)" -w "$$image" -v \
			$(STM32_PROGRAM_EXTRA_ARGS); \
	done

ai-load: build
	+$(MAKE) ai-load-weights
	+$(MAKE) ai-load-blobs

ai-init: ai-load

attach:
	./usb_attach.sh

monitor:
	sh tools/uart-monitor.sh

thread-monitor-dump: build
	@test "$(APP_TARGET)" = "sample-ai" || { \
		echo "thread-monitor-dump requires APP_TARGET=sample-ai" >&2; exit 2; \
	}
	@test -x "$(STM32_PROGRAMMER_CLI)" || { \
		echo "STM32_PROGRAMMER_CLI is not executable: $(STM32_PROGRAMMER_CLI)" >&2; exit 2; \
	}
	@test -n "$$(command -v "$(THREAD_MONITOR_NM)" 2>/dev/null)" || { \
		echo "THREAD_MONITOR_NM is not available: $(THREAD_MONITOR_NM)" >&2; exit 2; \
	}
	@test -f "$(THREAD_MONITOR_ELF)" || { \
		echo "ThreadMonitor ELF is missing: $(THREAD_MONITOR_ELF)" >&2; exit 2; \
	}
	@mkdir -p "$(dir $(THREAD_MONITOR_DUMP))"
	@set -eu; \
	start="$$("$(THREAD_MONITOR_NM)" -n "$(THREAD_MONITOR_ELF)" | \
		awk '$$3 == "__sample_ai_thread_monitor_start__" { print "0x" $$1; exit }')"; \
	end="$$("$(THREAD_MONITOR_NM)" -n "$(THREAD_MONITOR_ELF)" | \
		awk '$$3 == "__sample_ai_thread_monitor_end__" { print "0x" $$1; exit }')"; \
	test -n "$$start" || { echo "ThreadMonitor start symbol was not found" >&2; exit 2; }; \
	test -n "$$end" || { echo "ThreadMonitor end symbol was not found" >&2; exit 2; }; \
	size=$$((end - start)); \
	test "$$size" -gt 0 || { echo "ThreadMonitor region size is invalid" >&2; exit 2; }; \
	echo "Dumping ThreadMonitor: address=$$start size=$$(printf '0x%x' "$$size") output=$(THREAD_MONITOR_DUMP)"; \
	resume_board() { \
		"$(STM32_PROGRAMMER_CLI)" -c "$(THREAD_MONITOR_CONNECTION)" -run >/dev/null 2>&1 || true; \
	}; \
	trap resume_board EXIT; \
	"$(STM32_PROGRAMMER_CLI)" -c "$(THREAD_MONITOR_CONNECTION)" \
		-halt -u "$$start" "$$size" "$(THREAD_MONITOR_DUMP)"; \
	trap - EXIT; \
	resume_board; \
	echo "ThreadMonitor dump written: $(THREAD_MONITOR_DUMP)"

thread-monitor: thread-monitor-dump
	@mkdir -p "$(dir $(THREAD_MONITOR_JSON))"
	$(THREAD_MONITOR_PYTHON) userspace/sample-ai/tools/decode_thread_monitor.py \
		"$(THREAD_MONITOR_DUMP)" --output "$(THREAD_MONITOR_JSON)"
	$(THREAD_MONITOR_PYTHON) userspace/sample-ai/tools/analyze_npu_trace.py \
		"$(THREAD_MONITOR_JSON)" --cpu-hz "$(THREAD_MONITOR_CPU_HZ)"
	@command -v "$(THREAD_MONITOR_UV)" >/dev/null 2>&1 || { \
		echo "THREAD_MONITOR_UV is not available: $(THREAD_MONITOR_UV)" >&2; exit 2; \
	}
	@mkdir -p "$(dir $(THREAD_MONITOR_PNG))"
	MPLCONFIGDIR="$(BUILD_DIR)/matplotlib" \
	$(if $(strip $(THREAD_MONITOR_LD_PRELOAD)),LD_PRELOAD="$(THREAD_MONITOR_LD_PRELOAD)") \
	$(THREAD_MONITOR_UV) run --project userspace/sample-ai/tools \
		python userspace/sample-ai/tools/visualize_thread_monitor.py \
		"$(THREAD_MONITOR_JSON)" --output "$(THREAD_MONITOR_PNG)" \
		--cpu-hz "$(THREAD_MONITOR_CPU_HZ)"
	@echo "ThreadMonitor PNG written: $(THREAD_MONITOR_PNG)"

ram-run: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target ram-run

ram-load: ram-run

ai-run: ai-load
	+$(MAKE) ram-run

sign: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target stm32-sign

program: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target program

flash: program

run: ram-run

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean
