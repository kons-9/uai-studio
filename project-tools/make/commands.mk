.PHONY: help setup init configure generate cubemx-generate build monitor \
	ram-run ram-load sign program flash run clean uai-settings-force

HELP_TARGETS := configure generate build setup monitor ram-run sign program clean
HELP_configure := Configure CMake
HELP_generate := Generate required STM32Cube sources
HELP_build := Build $(APP_TARGET)
HELP_setup := Prepare dependencies and CMake
HELP_monitor := Open the configured UART monitor
HELP_ram-run := Build and load $(APP_TARGET)
HELP_sign := Create an STM32N6 signed image
HELP_program := Build and write the application image
HELP_clean := Clean the CMake build tree

help:
	@$(foreach help_target,$(HELP_TARGETS),printf '%s\n' "$(MAKE) -f $(SAMPLE_MAKEFILE) $(help_target) - $(HELP_$(help_target))";)
	@printf '\nHost settings: %s\n' "$(CONFIG_FILE)"

$(CUBEMX_SETTINGS_FILE): uai-settings-force
	@mkdir -p "$(CUBEMX_OUTPUT_DIR)"
	@printf '%s\n' "$(CUBEMX_EXECUTABLE)" "$(CUBEMX_IOC)" "$(CUBEMX_OUTPUT_DIR)" >"$@.tmp"
	@if cmp -s "$@.tmp" "$@"; then rm -f "$@.tmp"; else mv -f "$@.tmp" "$@"; fi

$(CUBEMX_OUTPUTS) $(CUBEMX_STAMP) &: $(CUBEMX_IOC) $(CUBEMX_SETTINGS_FILE) \
	$(UAI_SCRIPTS_DIR)/cubemx-generate.sh $(UAI_MAKE_DIR)/defaults.mk \
	$(UAI_MAKE_DIR)/commands.mk $(wildcard $(CONFIG_FILE))
	@rm -f "$(CUBEMX_STAMP)"
	sh "$(UAI_SCRIPTS_DIR)/cubemx-generate.sh"
	@set -eu; \
	for output in $(foreach output,$(CUBEMX_OUTPUTS),"$(output)"); do \
		test -f "$$output" || { echo "error: CubeMX output is missing: $$output" >&2; exit 2; }; \
	done; \
	touch $(foreach output,$(CUBEMX_OUTPUTS),"$(output)") "$(CUBEMX_STAMP)"

cubemx-generate: $(CUBEMX_OUTPUTS) $(CUBEMX_STAMP)
generate: cubemx-generate

configure: cubemx-generate
	$(CMAKE) $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --target $(APP_TARGET)

setup: $(if $(filter 1,$(ENABLE_AI)),ai-models .WAIT) configure
init: setup

ram-run: build
	$(CMAKE) --build "$(BUILD_DIR)" --target ram-run

ram-load run: ram-run

sign: build
	$(CMAKE) --build "$(BUILD_DIR)" --target stm32-sign

program: build
	$(CMAKE) --build "$(BUILD_DIR)" --target program

flash: program

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean

monitor:
	sh "$(UAI_SCRIPTS_DIR)/uart-monitor.sh"

ifeq ($(ENABLE_AI),1)
AI_MODEL_TARGETS := $(addprefix ai-model-,$(sort person segmentation face $(AI_MODEL_NAMES)))
.PHONY: ai-deps $(AI_MODEL_TARGETS) ai-models ai-build \
	ai-load-weights ai-load-blobs ai-load ai-init ai-run
HELP_TARGETS += ai-deps ai-models ai-build ai-load ai-run
HELP_ai-deps := Check AI dependencies
HELP_ai-models := Download and generate AI models
HELP_ai-build := Generate AI models and build
HELP_ai-load := Write AI weights and blobs
HELP_ai-run := Write AI data, then run in RAM

ai-deps:
	sh "$(AI_DEPS_SCRIPT)" $(if $(strip $(STEDGEAI_LIB_DIR)),"$(STEDGEAI_LIB_DIR)")

$(AI_MODEL_TARGETS): ai-model-%: ai-deps
	PATH="$(STEDGEAI_BIN):$$PATH" sh "$(AI_MODEL_GENERATOR)" $*

ai-models: $(AI_MODEL_BUILD_TARGETS)
ai-build: ai-models .WAIT build

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

ai-load: build .WAIT ai-load-weights .WAIT ai-load-blobs
ai-init: ai-load
ai-run: ai-load .WAIT ram-run
endif

ifeq ($(ENABLE_THREAD_MONITOR),1)
.PHONY: thread-monitor-dump thread-monitor
HELP_TARGETS += thread-monitor
HELP_thread-monitor := Dump and analyze ThreadMonitor

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
.PHONY: cpu-task-monitor-dump cpu-task-monitor
HELP_TARGETS += cpu-task-monitor
HELP_cpu-task-monitor := Dump and visualize CPU monitor

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
.PHONY: ui-layout ui-layout-check ui-designer
HELP_TARGETS += ui-designer ui-layout
HELP_ui-designer := Open the browser UI layout editor
HELP_ui-layout := Regenerate the UI layout header

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