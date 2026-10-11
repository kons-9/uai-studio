PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SAMPLE_DIR := $(PROJECT_ROOT)/userspace/ai-app
SAMPLE_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
APP_TARGET ?= ai-app
BUILD_DIR ?= $(PROJECT_ROOT)/build-ai-app-person
SAMPLE_DEFAULT_IOC := $(SAMPLE_DIR)/config/stm32n6570-dk-ai-app.ioc
ENABLE_AI := 1
ENABLE_THREAD_MONITOR := 1
ENABLE_CPU_TASK_MONITOR := 1
UI_LAYOUT_JSON := $(SAMPLE_DIR)/config/ui_layout.json
UI_LAYOUT_HEADER := $(SAMPLE_DIR)/src/ui/ui_layout.hpp
UI_FEATURE_CATALOG := $(SAMPLE_DIR)/config/ui_feature_catalog.json

include $(PROJECT_ROOT)/project-tools/make/common.mk

AI_VERIFY_PYTHON ?= python3
AI_VERIFY_DIR ?= $(BUILD_DIR)/verify/$(shell date +%Y%m%d-%H%M%S)
AI_VERIFY_DIR := $(AI_VERIFY_DIR)
AI_VERIFY_SECONDS ?= 1800
AI_VERIFY_STEP_SECONDS ?= 10
AI_VERIFY_LOG ?=

.PHONY: verify-exposure verify-endurance verify-stats
HELP_TARGETS += verify-exposure verify-endurance verify-stats
HELP_verify-exposure := Check exposure return with the running firmware
HELP_verify-endurance := Run UART operations and record current-firmware stability
HELP_verify-stats := Analyze a saved UART log without accessing hardware

verify-exposure:
	$(AI_VERIFY_PYTHON) "$(SAMPLE_DIR)/tool/verify.py" exposure \
		--device "$(UART_DEVICE)" --baud "$(UART_BAUD)" \
		--log "$(call uai_path,$(AI_VERIFY_DIR))/exposure.log" \
		--output "$(call uai_path,$(AI_VERIFY_DIR))/exposure.json"

verify-endurance:
	$(AI_VERIFY_PYTHON) "$(SAMPLE_DIR)/tool/verify.py" endurance \
		--device "$(UART_DEVICE)" --baud "$(UART_BAUD)" \
		--seconds "$(AI_VERIFY_SECONDS)" --step-seconds "$(AI_VERIFY_STEP_SECONDS)" \
		--log "$(call uai_path,$(AI_VERIFY_DIR))/endurance.log" \
		--output "$(call uai_path,$(AI_VERIFY_DIR))/endurance.json"

verify-stats:
	@test -n "$(AI_VERIFY_LOG)" || { echo "set AI_VERIFY_LOG to a saved UART log" >&2; exit 2; }
	$(AI_VERIFY_PYTHON) "$(SAMPLE_DIR)/tool/verify.py" analyze \
		"$(call uai_path,$(AI_VERIFY_LOG))" --output "$(call uai_path,$(AI_VERIFY_DIR))/statistics.json"
