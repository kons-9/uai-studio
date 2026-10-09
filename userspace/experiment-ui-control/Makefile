include ../experiment-camera-control/experiment.mk

UI_TOOL_PORT ?= 8767
.PHONY: ui-tool
ui-tool:
	python3 -m tool.feature_constraints serve --port "$(UI_TOOL_PORT)" --no-browser