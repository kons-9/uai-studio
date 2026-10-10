SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
SAMPLE_DIR := $(patsubst %/,%,$(dir $(SAMPLE_MAKEFILE)))
PROJECT_ROOT := $(abspath $(SAMPLE_DIR)/../..)
APP_TARGET := $(notdir $(SAMPLE_DIR))
BUILD_DIR ?= $(PROJECT_ROOT)/build-$(APP_TARGET)
HOST_BUILD_DIR ?= $(PROJECT_ROOT)/build/$(APP_TARGET)-host
MODEL_LOAD_FIXTURE_DIR := $(BUILD_DIR)/userspace/$(APP_TARGET)/model-load-fixture
MODEL_LOADER_NPU ?= ON
MODEL_LOADER_MULTI ?= ON
MODEL_LOADER_MODELS ?= person;face;seg
MODEL_NAME ?= person
MODEL_LOAD_PAYLOAD_DIR = $(if $(filter ON on 1,$(MODEL_LOADER_NPU)),$(BUILD_DIR)/userspace/$(APP_TARGET)/model-package$(if $(filter ON on 1,$(MODEL_LOADER_MULTI)),/$(MODEL_NAME)),$(MODEL_LOAD_FIXTURE_DIR))
MODEL_MANIFEST = $(if $(MODEL_LOADER_MANIFEST),$(MODEL_LOADER_MANIFEST),$(MODEL_LOAD_PAYLOAD_DIR)/manifest.bin)
MODEL_WEIGHTS ?= $(MODEL_LOAD_PAYLOAD_DIR)/weights.bin
MODEL_BLOB ?= $(MODEL_LOAD_PAYLOAD_DIR)/blob.bin
MODEL_UPLOAD_TIMEOUT ?= 1200
MODEL_STARTUP_TIMEOUT ?= 60
MODEL_RUN ?= ON
MODEL_WAIT_READY ?= OFF
MODEL_PYTHON ?= python3
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
EXPERIMENT_PREKERNEL_READY ?= ON
include $(PROJECT_ROOT)/project-tools/make/common.mk
ifeq ($(strip $(STEDGEAI_LIB_DIR)),)
STEDGEAI_LIB_DIR := /opt/ST/STEdgeAI/4.0/Middlewares/ST/AI
CMAKE_ARGS += -DSTEDGEAI_LIB_DIR="$(STEDGEAI_LIB_DIR)"
endif
MODEL_STEDGEAI ?= $(if $(STEDGEAI_BIN),$(STEDGEAI_BIN)/stedgeai,stedgeai)
CMAKE_ARGS += -DEXPERIMENT_PREKERNEL_READY=$(EXPERIMENT_PREKERNEL_READY)
CMAKE_ARGS += -DMODEL_LOADER_MANIFEST="$(MODEL_LOADER_MANIFEST)"
CMAKE_ARGS += -DMODEL_LOADER_NPU=$(MODEL_LOADER_NPU)
CMAKE_ARGS += -DMODEL_LOADER_MULTI=$(MODEL_LOADER_MULTI)
CMAKE_ARGS += -DMODEL_LOADER_MODELS="$(MODEL_LOADER_MODELS)"
.PHONY: test model-fixture model-generate model-package model-upload
test:
	$(CMAKE) -S "$(SAMPLE_DIR)" -B "$(HOST_BUILD_DIR)"
	$(if $(filter-out python3,$(MODEL_PYTHON)),$(CMAKE) -S "$(SAMPLE_DIR)" -B "$(HOST_BUILD_DIR)" -DPython3_EXECUTABLE="$(MODEL_PYTHON)")
	$(CMAKE) --build "$(HOST_BUILD_DIR)" --parallel 2
	ctest --test-dir "$(HOST_BUILD_DIR)" --output-on-failure

model-fixture:
	$(MODEL_PYTHON) "$(SAMPLE_DIR)/tool/fixture.py" --output-dir "$(MODEL_LOAD_FIXTURE_DIR)"

model-generate:
	$(MODEL_PYTHON) "$(SAMPLE_DIR)/tool/model.py" generate --model "$(MODEL_SOURCE)" \
		--stedgeai "$(MODEL_STEDGEAI)" --runtime-version "$(or $(MODEL_RUNTIME_VERSION),1201)" \
		$(if $(MODEL_DESCRIPTOR),--name "$(MODEL_NAME)" --descriptor "$(MODEL_DESCRIPTOR)",--kind "$(or $(MODEL_KIND),1)" --input-bytes "$(MODEL_INPUT_BYTES)" --output-bytes "$(MODEL_OUTPUT_BYTES)") \
		$(if $(MODEL_SEMANTICS),--semantics "$(MODEL_SEMANTICS)")

model-package:
	@test -n "$(filter ON on 1,$(MODEL_LOADER_NPU))" || { echo "model-package requires MODEL_LOADER_NPU=ON" >&2; exit 2; }
	+$(MAKE) -f "$(SAMPLE_MAKEFILE)" build MODEL_LOADER_NPU="$(MODEL_LOADER_NPU)"

# Start this in the UART terminal before ram-run in another terminal. The
# upload process owns the UART and also reports startup and camera messages.
model-upload: $(if $(MODEL_LOADER_MANIFEST),,$(if $(filter ON on 1,$(MODEL_LOADER_NPU)),model-package,model-fixture))
	@test -z "$(MODEL_LOADER_MANIFEST)" || { test "$(origin MODEL_WEIGHTS)" != file && test "$(origin MODEL_BLOB)" != file; } || { echo "Custom manifests require MODEL_WEIGHTS and MODEL_BLOB" >&2; exit 2; }
	@test "$(UART_DEVICE)" != auto || { echo "Set UART_DEVICE to the board's serial device" >&2; exit 2; }
	$(if $(filter ON on 1,$(MODEL_LOADER_NPU)),@test -x "$(STM32_PROGRAMMER_CLI)" || { echo "STM32_PROGRAMMER_CLI must be executable for direct PSRAM loading" >&2; exit 2; })
	$(MODEL_PYTHON) "$(SAMPLE_DIR)/tool/upload.py" --uart "$(UART_DEVICE)" \
		--manifest "$(MODEL_MANIFEST)" \
		--weights "$(MODEL_WEIGHTS)" \
		--blob "$(MODEL_BLOB)" --baud "$(UART_BAUD)" --timeout "$(MODEL_UPLOAD_TIMEOUT)" \
		--startup-timeout "$(MODEL_STARTUP_TIMEOUT)" $(if $(filter ON on 1,$(MODEL_WAIT_READY)),--wait-ready) --monitor-after-upload \
		$(if $(filter ON on 1,$(MODEL_LOADER_NPU)),--direct-programmer "$(STM32_PROGRAMMER_CLI)" $(if $(STM32_PROGRAM_SERIAL),--stlink-serial "$(STM32_PROGRAM_SERIAL)")) \
		$(if $(and $(filter ON on 1,$(MODEL_LOADER_NPU)),$(filter ON on 1,$(MODEL_RUN))),--run) \
		$(if $(MODEL_INPUT),--input "$(MODEL_INPUT)") $(if $(MODEL_IMAGE),--image "$(MODEL_IMAGE)") \
		$(if $(filter ON on 1,$(MODEL_INPUT_ZEROS)),--input-zeros) \
		$(if $(MODEL_RESULT_DIR),--result-dir "$(MODEL_RESULT_DIR)") \
		$(if $(MODEL_POLICY),--policy "$(MODEL_POLICY)") \
		$(if $(and $(filter ON on 1,$(MODEL_LOADER_NPU)),$(filter ON on 1,$(MODEL_RUN)),$(MODEL_EXPECTED_OUTPUT_CRC)),--expected-output-crc "$(MODEL_EXPECTED_OUTPUT_CRC)")
