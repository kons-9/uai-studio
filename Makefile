.DEFAULT_GOAL := help

APP_TARGET ?= sample0
BUILD_DIR ?= build
CMAKE ?= cmake
PYTHON ?= python3
USBIP_BUSID ?= 4-8

# RAM development load.  The CMSIS-DAP pack is not part of this repository.
PYOCD_PACK ?=

# Optional STM32 command-line tools.  These are passed to CMake only when
# supplied, so normal configure/build does not require CubeMX or CubeProgrammer.
CUBEMX_EXECUTABLE ?= STM32CubeMX
CUBEMX_SCRIPT ?=
CUBEMX_WORKING_DIRECTORY ?=
STM32_SIGNING_TOOL_CLI ?= STM32_SigningTool_CLI
STM32_SIGN_INPUT ?=
STM32_SIGN_OUTPUT ?=
STM32_SIGNING_ARGS ?=
STM32_PROGRAMMER_CLI ?= STM32_Programmer_CLI
STM32_EXTERNAL_LOADER ?=
STM32_PROGRAM_IMAGE ?=
STM32_PROGRAM_ADDRESS ?=0x70000000
STM32_PROGRAM_PORT ?=swd
STM32_PROGRAM_EXTRA_ARGS ?=

CMAKE_ARGS := -S . -B $(BUILD_DIR) -DAPP_TARGET=$(APP_TARGET)
ifneq ($(strip $(PYOCD_PACK)),)
CMAKE_ARGS += -DPYOCD_PACK="$(PYOCD_PACK)"
endif
ifneq ($(strip $(CUBEMX_EXECUTABLE)),)
CMAKE_ARGS += -DCUBEMX_EXECUTABLE="$(CUBEMX_EXECUTABLE)"
endif
ifneq ($(strip $(CUBEMX_SCRIPT)),)
CMAKE_ARGS += -DCUBEMX_SCRIPT="$(CUBEMX_SCRIPT)"
endif
ifneq ($(strip $(CUBEMX_WORKING_DIRECTORY)),)
CMAKE_ARGS += -DCUBEMX_WORKING_DIRECTORY="$(CUBEMX_WORKING_DIRECTORY)"
endif
ifneq ($(strip $(STM32_SIGNING_TOOL_CLI)),)
CMAKE_ARGS += -DSTM32_SIGNING_TOOL_CLI="$(STM32_SIGNING_TOOL_CLI)"
endif
ifneq ($(strip $(STM32_SIGN_INPUT)),)
CMAKE_ARGS += -DSTM32_SIGN_INPUT="$(STM32_SIGN_INPUT)"
endif
ifneq ($(strip $(STM32_SIGN_OUTPUT)),)
CMAKE_ARGS += -DSTM32_SIGN_OUTPUT="$(STM32_SIGN_OUTPUT)"
endif
ifneq ($(strip $(STM32_SIGNING_ARGS)),)
CMAKE_ARGS += -DSTM32_SIGNING_ARGS="$(STM32_SIGNING_ARGS)"
endif
ifneq ($(strip $(STM32_PROGRAMMER_CLI)),)
CMAKE_ARGS += -DSTM32_PROGRAMMER_CLI="$(STM32_PROGRAMMER_CLI)"
endif
ifneq ($(strip $(STM32_EXTERNAL_LOADER)),)
CMAKE_ARGS += -DSTM32_EXTERNAL_LOADER="$(STM32_EXTERNAL_LOADER)"
endif
ifneq ($(strip $(STM32_PROGRAM_IMAGE)),)
CMAKE_ARGS += -DSTM32_PROGRAM_IMAGE="$(STM32_PROGRAM_IMAGE)"
endif
ifneq ($(strip $(STM32_PROGRAM_ADDRESS)),)
CMAKE_ARGS += -DSTM32_PROGRAM_ADDRESS="$(STM32_PROGRAM_ADDRESS)"
endif
ifneq ($(strip $(STM32_PROGRAM_PORT)),)
CMAKE_ARGS += -DSTM32_PROGRAM_PORT="$(STM32_PROGRAM_PORT)"
endif
ifneq ($(strip $(STM32_PROGRAM_EXTRA_ARGS)),)
CMAKE_ARGS += -DSTM32_PROGRAM_EXTRA_ARGS="$(STM32_PROGRAM_EXTRA_ARGS)"
endif

.PHONY: help configure generate build attach ram-run sign program flash run clean

help:
	@echo "make configure  - Configure CMake"
	@echo "make generate   - Run STM32CubeMX CLI script"
	@echo "make build      - Build $(APP_TARGET)"
	@echo "make attach     - Attach the bound USB device to WSL"
	@echo "make ram-run    - Build, attach, and load $(APP_TARGET) into RAM"
	@echo "make sign       - Create an STM32N6 signed image"
	@echo "make program    - Write an external-flash image with STM32CubeProgrammer"
	@echo "make flash      - Alias for make program"
	@echo "make clean      - Clean the CMake build tree"
	@echo
	@echo "RAM example:"
	@echo "  make ram-run PYOCD_PACK=/tmp/Keil.STM32N6xx_DFP.1.2.0.pack"
	@echo
	@echo "External Flash example:"
	@echo "  make program STM32_EXTERNAL_LOADER=/absolute/path/N6.stldr"
	@echo "    STM32_PROGRAM_IMAGE=/absolute/path/image.bin"

configure:
	$(CMAKE) $(CMAKE_ARGS)

generate: configure
	$(CMAKE) --build $(BUILD_DIR) --target cubemx-generate

build: configure
	$(CMAKE) --build $(BUILD_DIR) --target $(APP_TARGET)

attach:
	USBIP_BUSID=$(USBIP_BUSID) ./usb_attach.sh

ram-run:
	@if [ -z "$(PYOCD_PACK)" ]; then \
		echo "error: PYOCD_PACK is required for make ram-run"; \
		echo "example: make ram-run PYOCD_PACK=/tmp/Keil.STM32N6xx_DFP.1.2.0.pack"; \
		exit 2; \
	fi
	+$(MAKE) build 'PYOCD_PACK=$(PYOCD_PACK)'
	+$(MAKE) attach USBIP_BUSID=$(USBIP_BUSID)
	$(CMAKE) --build $(BUILD_DIR) --target ram-run

sign: configure
	@if [ -z "$(STM32_SIGNING_TOOL_CLI)" ] || \
		[ -z "$(STM32_SIGN_INPUT)" ] || \
		[ -z "$(STM32_SIGN_OUTPUT)" ] || \
		[ -z "$(STM32_SIGNING_ARGS)" ]; then \
		echo "error: STM32_SIGNING_TOOL_CLI, STM32_SIGN_INPUT, STM32_SIGN_OUTPUT, and STM32_SIGNING_ARGS are required"; \
		exit 2; \
	fi
	$(CMAKE) --build $(BUILD_DIR) --target stm32-sign

program: configure
	@if [ -z "$(STM32_PROGRAMMER_CLI)" ] || \
		[ -z "$(STM32_EXTERNAL_LOADER)" ] || \
		[ -z "$(STM32_PROGRAM_IMAGE)" ]; then \
		echo "error: STM32_PROGRAMMER_CLI, STM32_EXTERNAL_LOADER, and STM32_PROGRAM_IMAGE are required"; \
		exit 2; \
	fi
	$(CMAKE) --build $(BUILD_DIR) --target program

flash: program

run: ram-run

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean
