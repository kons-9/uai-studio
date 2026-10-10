.DEFAULT_GOAL := help
UAI_MAKE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
include $(UAI_MAKE_DIR)/defaults.mk
include $(UAI_MAKE_DIR)/commands.mk
