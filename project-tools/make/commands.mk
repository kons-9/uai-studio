UAI_TARGETS := help setup init configure generate cubemx-generate build monitor \
	ram-run ram-load sign program flash run clean $(APP_TARGETS)

ifeq ($(ENABLE_AI),1)
UAI_TARGETS += ai-models ai-build ai-load-weights ai-load-blobs ai-load ai-init ai-run \
	$(addprefix ai-model-,$(AI_MODEL_NAMES))
endif
ifeq ($(ENABLE_THREAD_MONITOR),1)
UAI_TARGETS += thread-monitor-dump thread-monitor
endif
ifeq ($(ENABLE_CPU_TASK_MONITOR),1)
UAI_TARGETS += cpu-task-monitor-dump cpu-task-monitor
endif
ifneq ($(strip $(UI_LAYOUT_JSON)),)
UAI_TARGETS += ui-layout ui-layout-check ui-designer
endif

export UAI_TARGETS
UAI_REQUESTED_TARGETS := $(filter $(UAI_TARGETS),$(MAKECMDGOALS))
.PHONY: $(UAI_TARGETS) uai-dispatch
$(UAI_TARGETS): uai-dispatch

uai-dispatch:
	$(PYTHON) "$(UAI_SCRIPTS_DIR)/app_task.py" $(or $(UAI_REQUESTED_TARGETS),help)
