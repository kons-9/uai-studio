UAI_PROJECT_ROOT := $(abspath $(UAI_MAKE_DIR)/../..)
PROJECT_ROOT ?= $(UAI_PROJECT_ROOT)
override PROJECT_ROOT := $(abspath $(if $(filter /%,$(PROJECT_ROOT)),$(PROJECT_ROOT),$(UAI_PROJECT_ROOT)/$(PROJECT_ROOT)))
uai_path = $(if $(strip $(1)),$(abspath $(if $(filter /%,$(1)),$(1),$(PROJECT_ROOT)/$(1))))

APP_TARGET ?= experiment-hello-world
SAMPLE_DIR ?= $(PROJECT_ROOT)/userspace/$(APP_TARGET)
SAMPLE_MAKEFILE ?= $(SAMPLE_DIR)/Makefile
BUILD_DIR ?= $(PROJECT_ROOT)/build-$(APP_TARGET)
CONFIG_FILE ?= $(PROJECT_ROOT)/project-tools/host-config/local.mk
override CONFIG_FILE := $(call uai_path,$(CONFIG_FILE))
-include $(CONFIG_FILE)

$(foreach path_variable,SAMPLE_DIR SAMPLE_MAKEFILE BUILD_DIR,\
	$(eval override $(path_variable) := $(call uai_path,$($(path_variable)))))
ifneq ($(APP_TARGET),$(notdir $(SAMPLE_DIR)))
$(error APP_TARGET=$(APP_TARGET) does not match this sample; invoke the Makefile under userspace/$(notdir $(SAMPLE_DIR)) without APP_TARGET)
endif

HOST_APP_DIR ?= $(PROJECT_ROOT)/host_app
UAI_SCRIPTS_DIR := $(abspath $(UAI_MAKE_DIR)/../scripts)
CMAKE ?= cmake
# STM32CubeMX_PATH is set by the installed Linux CubeMX launcher on this host.
# Prefer its explicit executable path over relying on PATH, which may not
# include the installation directory when invoked from a sample Makefile.
CUBEMX_EXECUTABLE ?= $(if $(strip $(STM32CubeMX_PATH)),$(STM32CubeMX_PATH)/STM32CubeMX,STM32CubeMX)
SAMPLE_DEFAULT_IOC ?= $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
CUBEMX_IOC ?= $(SAMPLE_DEFAULT_IOC)
CUBEMX_OUTPUT_DIR ?= $(BUILD_DIR)/cubemx
STEDGEAI_LIB_DIR ?=
AI_VISION_MODELS_PP_DIR ?=
UART_DEVICE ?= auto
UART_BAUD ?= 115200

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

UI_LAYOUT_JSON ?=
UI_LAYOUT_HEADER ?=
UI_DESIGNER_PORT ?= 8765
UI_DESIGNER_PYTHON ?= python3

STEDGEAI_BIN ?= /opt/ST/STEdgeAI/4.0/Utilities/linux
AI_MODELS_DIR ?= $(SAMPLE_DIR)/models
AI_MODEL_NAMES ?= person segmentation face
AI_MODEL_GENERATOR ?= $(AI_MODELS_DIR)/../tool/generate_model.sh
AI_BUILD_DIR ?= $(BUILD_DIR)/userspace/$(APP_TARGET)
AI_MODEL_OPTIMIZATION ?= balanced
AI_MODEL_INPUT_DATA_TYPE ?= uint8
AI_MODEL_OUTPUT_DATA_TYPE ?= int8
AI_MODEL_INPUTS_CH_POSITION ?= chlast
AI_MODEL_OUTPUTS_CH_POSITION ?=
AI_MODEL_C_API ?= st-ai
AI_MODEL_CUT_OUTPUT_TENSORS ?=
AI_MODEL_NETWORK_ADDRESS ?=
AI_MODEL_DOWNLOAD_URL ?=

STM32_PROGRAMMER_CLI ?= $(if $(strip $(STM32_PROGRAMMER_ROOT)),$(STM32_PROGRAMMER_ROOT)/bin/STM32_Programmer_CLI)
STM32_EXTERNAL_LOADER ?= $(if $(strip $(STM32_PROGRAMMER_ROOT)),$(STM32_PROGRAMMER_ROOT)/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr)
STM32_PROGRAM_PORT ?= swd

UAI_PATH_VARIABLES := HOST_APP_DIR SAMPLE_DEFAULT_IOC CUBEMX_IOC CUBEMX_OUTPUT_DIR \
	ARM_NONE_EABI_TOOLCHAIN_PATH STM32CUBE_N6_DIR STEDGEAI_LIB_DIR AI_VISION_MODELS_PP_DIR \
	STEDGEAI_BIN AI_MODELS_DIR AI_MODEL_GENERATOR AI_BUILD_DIR \
	STM32_PROGRAMMER_ROOT STM32_PROGRAMMER_LIB STM32_EXTERNAL_LOADER \
	STM32_SIGN_INPUT STM32_SIGN_OUTPUT STM32_PROGRAM_IMAGE STM32_RAM_IMAGE \
	THREAD_MONITOR_ELF THREAD_MONITOR_DUMP THREAD_MONITOR_JSON THREAD_MONITOR_PNG \
	THREAD_MONITOR_LD_PRELOAD CPU_TASK_MONITOR_DUMP CPU_TASK_MONITOR_JSON \
	CPU_TASK_MONITOR_PNG CPU_TASK_MONITOR_CSV UI_LAYOUT_JSON UI_LAYOUT_HEADER
$(foreach path_variable,$(UAI_PATH_VARIABLES),\
	$(eval override $(path_variable) := $(call uai_path,$($(path_variable)))))
UAI_TOOL_VARIABLES := CMAKE CUBEMX_EXECUTABLE STM32_PROGRAMMER_CLI \
	STM32_SIGNING_TOOL_CLI THREAD_MONITOR_NM THREAD_MONITOR_PYTHON \
	THREAD_MONITOR_UV UI_DESIGNER_PYTHON
$(foreach tool_variable,$(UAI_TOOL_VARIABLES),\
	$(eval override $(tool_variable) := $(if $(findstring /,$($(tool_variable))),\
		$(call uai_path,$($(tool_variable))),$($(tool_variable)))))

AI_PROGRAM_CONNECTION := port=$(STM32_PROGRAM_PORT)$(if $(strip $(STM32_PROGRAM_SERIAL)), sn=$(STM32_PROGRAM_SERIAL))
AI_MODEL_BUILD_TARGETS := $(addprefix ai-model-,$(AI_MODEL_NAMES))
AI_WEIGHT_IMAGES := $(foreach name,$(AI_MODEL_NAMES),$(AI_MODELS_DIR)/$(name)/network_data.hex)
AI_BLOB_IMAGES := $(foreach name,$(AI_MODEL_NAMES),$(AI_BUILD_DIR)/network_blobs_$(name).hex)

CUBEMX_SETTINGS_FILE := $(CUBEMX_OUTPUT_DIR)/.uai-settings
CUBEMX_STAMP := $(CUBEMX_OUTPUT_DIR)/.uai-generated
CUBEMX_OUTPUTS ?= $(addprefix $(CUBEMX_OUTPUT_DIR)/,\
	FSBL/Core/Inc/main.h \
	FSBL/Core/Inc/stm32n6xx_hal_conf.h \
	FSBL/Core/Src/main.c \
	FSBL/Core/Src/extmem_manager.c \
	FSBL/Core/Src/stm32n6xx_hal_msp.c \
	FSBL/Core/Src/stm32n6xx_it.c \
	FSBL/Core/Src/system_stm32n6xx_fsbl.c \
	FSBL/Core/Src/syscalls.c \
	FSBL/Core/Src/sysmem.c \
	FSBL/Core/Startup/startup_stm32n657x0hxq_fsbl.s \
	Appli/Core/Inc/main.h \
	Drivers/STM32N6xx_HAL_Driver/Inc/stm32n6xx_hal.h \
	Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c \
	$(if $(filter experiment-ai,$(APP_TARGET)),FSBL/STM32N657X0HXQ_AXISRAM2_fsbl.ld))

export ARM_NONE_EABI_TOOLCHAIN_PATH STM32CUBE_N6_DIR
export CUBEMX_EXECUTABLE CUBEMX_IOC CUBEMX_OUTPUT_DIR
export STEDGEAI_LIB_DIR AI_VISION_MODELS_PP_DIR
export STM32_SIGNING_TOOL_CLI STM32_SIGN_INPUT STM32_SIGN_OUTPUT STM32_SIGNING_ARGS
export STM32_PROGRAMMER_ROOT STM32_PROGRAMMER_CLI STM32_PROGRAMMER_LIB
export STM32_EXTERNAL_LOADER STM32_PROGRAM_IMAGE STM32_PROGRAM_ADDRESS
export STM32_PROGRAM_PORT STM32_PROGRAM_EXTRA_ARGS STM32_PROGRAM_SERIAL
export STM32_RAM_IMAGE STM32_RAM_ADDRESS STM32_RAM_ENTRY STM32_RAM_STACK STM32_RAM_XPSR
export UART_DEVICE UART_BAUD STEDGEAI_BIN
export AI_MODEL_OPTIMIZATION AI_MODEL_INPUT_DATA_TYPE AI_MODEL_OUTPUT_DATA_TYPE
export AI_MODEL_INPUTS_CH_POSITION AI_MODEL_OUTPUTS_CH_POSITION AI_MODEL_C_API
export AI_MODEL_CUT_OUTPUT_TENSORS AI_MODEL_NETWORK_ADDRESS AI_MODEL_DOWNLOAD_URL

CMAKE_ARGS := -S "$(PROJECT_ROOT)" -B "$(BUILD_DIR)" -DAPP_TARGET="$(APP_TARGET)"
CMAKE_ARGS += -DUAI_CPU_TASK_MONITOR=$(if $(filter 1,$(ENABLE_CPU_TASK_MONITOR)),ON,OFF)
CMAKE_ARGS += $(foreach setting,STM32CUBE_N6_DIR STEDGEAI_LIB_DIR AI_VISION_MODELS_PP_DIR,$(if $(strip $($(setting))),-D$(setting)="$($(setting))"))
