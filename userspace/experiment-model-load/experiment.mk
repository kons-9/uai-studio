SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
SAMPLE_DIR := $(patsubst %/,%,$(dir $(SAMPLE_MAKEFILE)))
PROJECT_ROOT := $(abspath $(SAMPLE_DIR)/../..)
APP_TARGET := $(notdir $(SAMPLE_DIR))
BUILD_DIR ?= $(PROJECT_ROOT)/build-$(APP_TARGET)
HOST_BUILD_DIR ?= $(PROJECT_ROOT)/build/$(APP_TARGET)-host
MODEL_LOAD_FIXTURE_DIR := $(BUILD_DIR)/userspace/$(APP_TARGET)/model-load-fixture
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-fullsecure.ioc
ENABLE_AI := 0
ENABLE_THREAD_MONITOR := 0
ENABLE_CPU_TASK_MONITOR := 0
EXPERIMENT_PREKERNEL_READY ?= ON
include $(PROJECT_ROOT)/build-system/make/common.mk
CMAKE_ARGS += -DEXPERIMENT_PREKERNEL_READY=$(EXPERIMENT_PREKERNEL_READY)
CMAKE_ARGS += -DEXPERIMENT_MODEL_MANIFEST="$(EXPERIMENT_MODEL_MANIFEST)"
.PHONY: test model-fixture model-upload
test:
	$(CMAKE) -S "$(SAMPLE_DIR)" -B "$(HOST_BUILD_DIR)"
	$(CMAKE) --build "$(HOST_BUILD_DIR)" --parallel 2
	ctest --test-dir "$(HOST_BUILD_DIR)" --output-on-failure

model-fixture:
	python3 "$(SAMPLE_DIR)/fixture.py" --output-dir "$(MODEL_LOAD_FIXTURE_DIR)"

# Start this in the UART terminal before ram-run in another terminal. The
# upload process owns the UART and also reports startup and camera messages.
model-upload: model-fixture
	@test -z "$(EXPERIMENT_MODEL_MANIFEST)" || { echo "model-upload uses the built-in demo fixture; unset EXPERIMENT_MODEL_MANIFEST" >&2; exit 2; }
	@test "$(UART_DEVICE)" != auto || { echo "Set UART_DEVICE to the board's serial device" >&2; exit 2; }
	python3 "$(SAMPLE_DIR)/upload.py" --uart "$(UART_DEVICE)" \
		--manifest "$(MODEL_LOAD_FIXTURE_DIR)/manifest.bin" \
		--weights "$(MODEL_LOAD_FIXTURE_DIR)/weights.bin" \
		--blob "$(MODEL_LOAD_FIXTURE_DIR)/blob.bin" --wait-ready --monitor-after-upload
