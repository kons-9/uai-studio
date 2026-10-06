# Shared command layer for the per-sample Makefiles.
#
# Each sample owns the user-facing Makefile and defines PROJECT_ROOT,
# SAMPLE_DIR, SAMPLE_MAKEFILE, APP_TARGET, BUILD_DIR, and the feature flags
# before including this file.  Paths are kept absolute so both
# `make -C /absolute/path/to/userspace/<sample>` and
# `make -f /absolute/path/to/userspace/<sample>/Makefile` work.

.DEFAULT_GOAL := help

HOST_APP_DIR := $(PROJECT_ROOT)/host_app

CONFIG_FILE ?= $(PROJECT_ROOT)/build-system/host-config/local.mk
ifneq ($(filter /%,$(CONFIG_FILE)),)
else
CONFIG_FILE := $(PROJECT_ROOT)/$(CONFIG_FILE)
endif
-include $(CONFIG_FILE)

ifneq ($(APP_TARGET),$(notdir $(SAMPLE_DIR)))
$(error APP_TARGET=$(APP_TARGET) does not match this sample; invoke the Makefile under userspace/$(notdir $(SAMPLE_DIR)) without APP_TARGET)
endif

CMAKE ?= cmake
SAMPLE_DEFAULT_IOC ?= $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
CUBEMX_IOC ?= $(SAMPLE_DEFAULT_IOC)
CUBEMX_OUTPUT_DIR ?= $(BUILD_DIR)/cubemx
STEDGEAI_LIB_DIR ?=
AI_VISION_MODELS_PP_DIR ?=
UART_DEVICE ?= auto
UART_BAUD ?= 115200

ifneq ($(filter /%,$(CUBEMX_IOC)),)
else
CUBEMX_IOC := $(PROJECT_ROOT)/$(CUBEMX_IOC)
endif
ifneq ($(filter /%,$(CUBEMX_OUTPUT_DIR)),)
else
CUBEMX_OUTPUT_DIR := $(PROJECT_ROOT)/$(CUBEMX_OUTPUT_DIR)
endif

THREAD_MONITOR_ELF ?= $(BUILD_DIR)/userspace/$(APP_TARGET)/$(APP_TARGET).elf
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
CPU_TASK_MONITOR_DUMP ?= $(BUILD_DIR)/cpu_task_monitor.bin
CPU_TASK_MONITOR_JSON ?= $(BUILD_DIR)/cpu_task_monitor.json
CPU_TASK_MONITOR_PNG ?= $(BUILD_DIR)/cpu_task_monitor.png
CPU_TASK_MONITOR_CSV ?= $(BUILD_DIR)/cpu_task_monitor.csv

# Samples with an on-screen UI set UI_LAYOUT_JSON/UI_LAYOUT_HEADER to enable
# the ui_designer editor and header generation targets.
UI_LAYOUT_JSON ?=
UI_LAYOUT_HEADER ?=
UI_DESIGNER_PORT ?= 8765
UI_DESIGNER_PYTHON ?= python3

AI_PROGRAM_CONNECTION := port=$(STM32_PROGRAM_PORT)$(if $(strip $(STM32_PROGRAM_SERIAL)), sn=$(STM32_PROGRAM_SERIAL))

ifeq ($(ENABLE_AI),1)
STEDGEAI_BIN ?= /opt/ST/STEdgeAI/4.0/Utilities/linux
# Samples may share the model generator and generated artifacts of another
# sample by overriding AI_MODELS_DIR, and build a subset with AI_MODEL_NAMES.
AI_MODELS_DIR ?= $(SAMPLE_DIR)/models
AI_MODEL_NAMES ?= person segmentation face
AI_MODEL_GENERATOR := $(AI_MODELS_DIR)/generate_model.sh
AI_BUILD_DIR := $(BUILD_DIR)/userspace/$(APP_TARGET)
AI_MODEL_BUILD_TARGETS := $(addprefix ai-model-,$(AI_MODEL_NAMES))
AI_WEIGHT_IMAGES := $(foreach name,$(AI_MODEL_NAMES),$(AI_MODELS_DIR)/$(name)/network_data.hex)
AI_BLOB_IMAGES := $(foreach name,$(AI_MODEL_NAMES),$(AI_BUILD_DIR)/network_blobs_$(name).hex)

AI_MODEL_OPTIMIZATION ?= balanced
AI_MODEL_INPUT_DATA_TYPE ?= uint8
AI_MODEL_OUTPUT_DATA_TYPE ?= int8
AI_MODEL_INPUTS_CH_POSITION ?= chlast
AI_MODEL_OUTPUTS_CH_POSITION ?=
AI_MODEL_C_API ?= st-ai
AI_MODEL_CUT_OUTPUT_TENSORS ?=
AI_MODEL_NETWORK_ADDRESS ?=
AI_MODEL_DOWNLOAD_URL ?=
endif

ifneq ($(strip $(STM32_PROGRAMMER_ROOT)),)
STM32_PROGRAMMER_CLI ?= $(STM32_PROGRAMMER_ROOT)/bin/STM32_Programmer_CLI
STM32_EXTERNAL_LOADER ?= $(STM32_PROGRAMMER_ROOT)/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr
endif

# Keep host-specific values in build-system/host-config/local.mk while making them available to
# CMake and the helper scripts.
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

CMAKE_ARGS := -S "$(PROJECT_ROOT)" -B "$(BUILD_DIR)" -DAPP_TARGET="$(APP_TARGET)"
CMAKE_ARGS += -DUAI_CPU_TASK_MONITOR=$(if $(filter 1,$(ENABLE_CPU_TASK_MONITOR)),ON,OFF)
ifneq ($(strip $(STM32CUBE_N6_DIR)),)
CMAKE_ARGS += -DSTM32CUBE_N6_DIR="$(STM32CUBE_N6_DIR)"
endif
ifneq ($(strip $(STEDGEAI_LIB_DIR)),)
CMAKE_ARGS += -DSTEDGEAI_LIB_DIR="$(STEDGEAI_LIB_DIR)"
endif
ifneq ($(strip $(AI_VISION_MODELS_PP_DIR)),)
CMAKE_ARGS += -DAI_VISION_MODELS_PP_DIR="$(AI_VISION_MODELS_PP_DIR)"
endif

.PHONY: help setup init configure generate cubemx-generate build monitor \
	ram-run ram-load sign program flash run clean
.PHONY: ai-deps ai-model-person ai-model-segmentation ai-model-face \
	ai-models ai-build ai-load-weights ai-load-blobs ai-load ai-init ai-run
.PHONY: thread-monitor-dump thread-monitor cpu-task-monitor-dump cpu-task-monitor
.PHONY: ui-layout ui-layout-check ui-designer

help:
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) configure  - Configure CMake"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) generate   - Generate STM32Cube sources"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) build      - Build $(APP_TARGET)"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) setup      - Prepare dependencies and CMake"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) monitor    - Open the configured UART monitor"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ram-run    - Build and load $(APP_TARGET)"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) sign       - Create an STM32N6 signed image"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) program    - Write the application image"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) clean      - Clean the CMake build tree"
ifeq ($(ENABLE_AI),1)
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ai-deps - Check AI dependencies"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ai-models  - Download and generate AI models"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ai-build   - Generate AI models and build"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ai-load    - Write AI weights and blobs"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ai-run     - Write AI data, then run in RAM"
endif
ifeq ($(ENABLE_THREAD_MONITOR),1)
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) thread-monitor - Dump and analyze ThreadMonitor"
endif
ifeq ($(ENABLE_CPU_TASK_MONITOR),1)
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) cpu-task-monitor - Dump and visualize CPU monitor"
endif
ifneq ($(strip $(UI_LAYOUT_JSON)),)
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ui-designer - Open the browser UI layout editor"
	@echo "$(MAKE) -f $(SAMPLE_MAKEFILE) ui-layout  - Regenerate the UI layout header"
endif
	@echo
	@echo "Host settings: $(CONFIG_FILE)"

setup:
ifeq ($(ENABLE_AI),1)
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" ai-deps
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" ai-models
endif
ifeq ($(CUBEMX_GENERATOR),script)
	CUBEMX_EXECUTABLE="$(CUBEMX_EXECUTABLE)" \
	CUBEMX_IOC="$(CUBEMX_IOC)" \
	CUBEMX_OUTPUT_DIR="$(CUBEMX_OUTPUT_DIR)" \
	sh "$(PROJECT_ROOT)/build-system/scripts/cubemx-generate.sh"
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" configure
else
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" cubemx-generate
endif

init: setup

configure:
	$(CMAKE) $(CMAKE_ARGS)

generate: cubemx-generate

# CubeMX output is required by the project CMake files, so generate it before
# the first configure.  After generation, configure the CMake build tree.
cubemx-generate:
	CUBEMX_EXECUTABLE="$(CUBEMX_EXECUTABLE)" \
	CUBEMX_IOC="$(CUBEMX_IOC)" \
	CUBEMX_OUTPUT_DIR="$(CUBEMX_OUTPUT_DIR)" \
	sh "$(PROJECT_ROOT)/build-system/scripts/cubemx-generate.sh"
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" configure

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target $(APP_TARGET)

ifeq ($(ENABLE_AI),1)
AI_DEPS_SCRIPT ?= $(SAMPLE_DIR)/scripts/setup_third_party.sh
ai-deps:
	@if test -n "$(strip $(STEDGEAI_LIB_DIR))"; then \
		sh "$(AI_DEPS_SCRIPT)" "$(STEDGEAI_LIB_DIR)"; \
	else \
		sh "$(AI_DEPS_SCRIPT)"; \
	fi

ai-model-person:
	PATH="$(STEDGEAI_BIN):$$PATH" sh "$(AI_MODEL_GENERATOR)" person

ai-model-segmentation:
	PATH="$(STEDGEAI_BIN):$$PATH" sh "$(AI_MODEL_GENERATOR)" segmentation

ai-model-face:
	PATH="$(STEDGEAI_BIN):$$PATH" sh "$(AI_MODEL_GENERATOR)" face

ai-models: $(AI_MODEL_BUILD_TARGETS)

ai-build: ai-models
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" build

ai-load-weights:
	@test -x "$(STM32_PROGRAMMER_CLI)" || { echo "STM32_PROGRAMMER_CLI is not executable: $(STM32_PROGRAMMER_CLI)" >&2; exit 2; }
	@test -f "$(STM32_EXTERNAL_LOADER)" || { echo "STM32_EXTERNAL_LOADER is missing: $(STM32_EXTERNAL_LOADER)" >&2; exit 2; }
	@set -eu; \
	for image in $(foreach image,$(AI_WEIGHT_IMAGES),"$(image)" ); do \
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
	for image in $(foreach image,$(AI_BLOB_IMAGES),"$(image)" ); do \
		test -f "$$image" || { echo "AI command blob is missing: $$image" >&2; exit 2; }; \
		echo "Programming AI command blob: $$image"; \
		"$(STM32_PROGRAMMER_CLI)" -c "$(AI_PROGRAM_CONNECTION)" \
			-el "$(STM32_EXTERNAL_LOADER)" -w "$$image" -v \
			$(STM32_PROGRAM_EXTRA_ARGS); \
	done

ai-load: build
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" ai-load-weights
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" ai-load-blobs

ai-init: ai-load

ai-run: ai-load
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" ram-run
endif

monitor:
	sh "$(PROJECT_ROOT)/build-system/scripts/uart-monitor.sh"

ifeq ($(ENABLE_THREAD_MONITOR),1)
thread-monitor-dump: build
	@test -x "$(STM32_PROGRAMMER_CLI)" || { echo "STM32_PROGRAMMER_CLI is not executable: $(STM32_PROGRAMMER_CLI)" >&2; exit 2; }
	@test -n "$$(command -v "$(THREAD_MONITOR_NM)" 2>/dev/null)" || { echo "THREAD_MONITOR_NM is not available: $(THREAD_MONITOR_NM)" >&2; exit 2; }
	@test -f "$(THREAD_MONITOR_ELF)" || { echo "ThreadMonitor ELF is missing: $(THREAD_MONITOR_ELF)" >&2; exit 2; }
	@mkdir -p "$(dir $(THREAD_MONITOR_DUMP))"
	@set -eu; \
	start="$$($(THREAD_MONITOR_NM) -n "$(THREAD_MONITOR_ELF)" | awk '$$3 == "__sample_ai_thread_monitor_start__" { print "0x" $$1; exit }')"; \
	end="$$($(THREAD_MONITOR_NM) -n "$(THREAD_MONITOR_ELF)" | awk '$$3 == "__sample_ai_thread_monitor_end__" { print "0x" $$1; exit }')"; \
	test -n "$$start" || { echo "ThreadMonitor start symbol was not found" >&2; exit 2; }; \
	test -n "$$end" || { echo "ThreadMonitor end symbol was not found" >&2; exit 2; }; \
	size=$$((end - start)); \
	test "$$size" -gt 0 || { echo "ThreadMonitor region size is invalid" >&2; exit 2; }; \
	echo "Dumping ThreadMonitor: address=$$start size=$$(printf '0x%x' "$$size") output=$(THREAD_MONITOR_DUMP)"; \
	resume_board() { "$(STM32_PROGRAMMER_CLI)" -c "$(THREAD_MONITOR_CONNECTION)" -run >/dev/null 2>&1 || true; }; \
	trap resume_board EXIT; \
	"$(STM32_PROGRAMMER_CLI)" -c "$(THREAD_MONITOR_CONNECTION)" -halt -u "$$start" "$$size" "$(THREAD_MONITOR_DUMP)"; \
	trap - EXIT; resume_board; \
	echo "ThreadMonitor dump written: $(THREAD_MONITOR_DUMP)"

thread-monitor: thread-monitor-dump
ifneq ($(filter ai-app mini-ai-app,$(APP_TARGET)),)
	MPLCONFIGDIR="$(BUILD_DIR)/matplotlib" \
	$(if $(strip $(THREAD_MONITOR_LD_PRELOAD)),LD_PRELOAD="$(THREAD_MONITOR_LD_PRELOAD)") \
	$(THREAD_MONITOR_UV) run --project "$(HOST_APP_DIR)" \
		python "$(HOST_APP_DIR)/ai_model_monitor/ai_model_monitor.py" all \
		"$(THREAD_MONITOR_DUMP)" --json "$(THREAD_MONITOR_JSON)" \
		--png "$(THREAD_MONITOR_PNG)" --cpu-hz "$(THREAD_MONITOR_CPU_HZ)"
	@echo "ThreadMonitor PNG written: $(THREAD_MONITOR_PNG)"
else
	@mkdir -p "$(dir $(THREAD_MONITOR_JSON))"
	$(THREAD_MONITOR_PYTHON) "$(SAMPLE_DIR)/tools/decode_thread_monitor.py" \
		"$(THREAD_MONITOR_DUMP)" --output "$(THREAD_MONITOR_JSON)"
	$(THREAD_MONITOR_PYTHON) "$(SAMPLE_DIR)/tools/analyze_npu_trace.py" \
		"$(THREAD_MONITOR_JSON)" --cpu-hz "$(THREAD_MONITOR_CPU_HZ)"
	@mkdir -p "$(dir $(THREAD_MONITOR_PNG))"
	MPLCONFIGDIR="$(BUILD_DIR)/matplotlib" \
	$(if $(strip $(THREAD_MONITOR_LD_PRELOAD)),LD_PRELOAD="$(THREAD_MONITOR_LD_PRELOAD)") \
	$(THREAD_MONITOR_UV) run --project "$(SAMPLE_DIR)/tools" \
		python "$(SAMPLE_DIR)/tools/visualize_thread_monitor.py" \
		"$(THREAD_MONITOR_JSON)" --output "$(THREAD_MONITOR_PNG)" \
		--cpu-hz "$(THREAD_MONITOR_CPU_HZ)"
	@echo "ThreadMonitor PNG written: $(THREAD_MONITOR_PNG)"
endif
endif

ifeq ($(ENABLE_CPU_TASK_MONITOR),1)
cpu-task-monitor-dump: build
	@test -x "$(STM32_PROGRAMMER_CLI)" || { echo "STM32_PROGRAMMER_CLI is not executable: $(STM32_PROGRAMMER_CLI)" >&2; exit 2; }
	@test -n "$$(command -v "$(THREAD_MONITOR_NM)" 2>/dev/null)" || { echo "THREAD_MONITOR_NM is not available: $(THREAD_MONITOR_NM)" >&2; exit 2; }
	@test -f "$(THREAD_MONITOR_ELF)" || { echo "CPU task monitor ELF is missing: $(THREAD_MONITOR_ELF)" >&2; exit 2; }
	@mkdir -p "$$(dirname "$(CPU_TASK_MONITOR_DUMP)")"
	@set -eu; \
	start="$$($(THREAD_MONITOR_NM) -n "$(THREAD_MONITOR_ELF)" | awk '$$3 == "__sample_ai_cpu_task_monitor_start__" { print "0x" $$1; exit }')"; \
	end="$$($(THREAD_MONITOR_NM) -n "$(THREAD_MONITOR_ELF)" | awk '$$3 == "__sample_ai_cpu_task_monitor_end__" { print "0x" $$1; exit }')"; \
	test -n "$$start" || { echo "CPU task monitor start symbol was not found" >&2; exit 2; }; \
	test -n "$$end" || { echo "CPU task monitor end symbol was not found" >&2; exit 2; }; \
	size=$$((end - start)); \
	test "$$size" -gt 0 || { echo "CPU task monitor region size is invalid" >&2; exit 2; }; \
	echo "Dumping CPU task monitor: address=$$start size=$$(printf '0x%x' "$$size") output=$(CPU_TASK_MONITOR_DUMP)"; \
	resume_board() { "$(STM32_PROGRAMMER_CLI)" -c "$(THREAD_MONITOR_CONNECTION)" -run >/dev/null 2>&1 || true; }; \
	trap resume_board EXIT; \
	"$(STM32_PROGRAMMER_CLI)" -c "$(THREAD_MONITOR_CONNECTION)" -halt -u "$$start" "$$size" "$(CPU_TASK_MONITOR_DUMP)"; \
	trap - EXIT; resume_board; \
	echo "CPU task monitor dump written: $(CPU_TASK_MONITOR_DUMP)"

cpu-task-monitor: cpu-task-monitor-dump
	@mkdir -p "$(dir $(CPU_TASK_MONITOR_PNG))" \
		"$(dir $(CPU_TASK_MONITOR_JSON))" "$(dir $(CPU_TASK_MONITOR_CSV))"
	MPLCONFIGDIR="$(BUILD_DIR)/matplotlib" UV_CACHE_DIR="$(BUILD_DIR)/uv-cache" \
	$(if $(strip $(THREAD_MONITOR_LD_PRELOAD)),LD_PRELOAD="$(THREAD_MONITOR_LD_PRELOAD)") \
	$(THREAD_MONITOR_UV) run --project "$(HOST_APP_DIR)" \
		python "$(HOST_APP_DIR)/cpu_task_monitor/cpu_task_monitor.py" \
		"$(CPU_TASK_MONITOR_DUMP)" -o "$(CPU_TASK_MONITOR_PNG)" \
		--json "$(CPU_TASK_MONITOR_JSON)" --csv "$(CPU_TASK_MONITOR_CSV)" \
		--cpu-hz "$(THREAD_MONITOR_CPU_HZ)"
	@echo "CPU task monitor PNG written: $(CPU_TASK_MONITOR_PNG)"
	@echo "CPU task monitor CSV written: $(CPU_TASK_MONITOR_CSV)"
endif

ifneq ($(strip $(UI_LAYOUT_JSON)),)
ui-layout:
	$(UI_DESIGNER_PYTHON) "$(HOST_APP_DIR)/ui_designer" generate \
		--layout "$(UI_LAYOUT_JSON)" --output "$(UI_LAYOUT_HEADER)"

ui-layout-check:
	$(UI_DESIGNER_PYTHON) "$(HOST_APP_DIR)/ui_designer" validate \
		--layout "$(UI_LAYOUT_JSON)" --check-font
	$(UI_DESIGNER_PYTHON) "$(HOST_APP_DIR)/ui_designer" generate --check \
		--layout "$(UI_LAYOUT_JSON)" --output "$(UI_LAYOUT_HEADER)"

ui-designer:
	$(UI_DESIGNER_PYTHON) "$(HOST_APP_DIR)/ui_designer" serve \
		--layout "$(UI_LAYOUT_JSON)" --port $(UI_DESIGNER_PORT)
endif

ram-run: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target ram-run

ram-load: ram-run

sign: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target stm32-sign

program: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target program

flash: program

run: ram-run

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean
