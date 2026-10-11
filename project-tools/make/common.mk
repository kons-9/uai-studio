.DEFAULT_GOAL := help
UAI_MAKE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
UAI_PROJECT_ROOT := $(abspath $(UAI_MAKE_DIR)/../..)
PROJECT_ROOT ?= $(UAI_PROJECT_ROOT)
override PROJECT_ROOT := $(abspath $(if $(filter /%,$(PROJECT_ROOT)),$(PROJECT_ROOT),$(UAI_PROJECT_ROOT)/$(PROJECT_ROOT)))
uai_path = $(if $(strip $(1)),$(abspath $(if $(filter /%,$(1)),$(1),$(PROJECT_ROOT)/$(1))))
SAMPLE_MAKEFILE ?= $(abspath $(firstword $(MAKEFILE_LIST)))
SAMPLE_DIR ?= $(patsubst %/,%,$(dir $(SAMPLE_MAKEFILE)))
override SAMPLE_DIR := $(call uai_path,$(SAMPLE_DIR))
APP_TARGET ?= $(notdir $(SAMPLE_DIR))
include $(SAMPLE_DIR)/config/build.mk
include $(UAI_MAKE_DIR)/defaults.mk
include $(UAI_MAKE_DIR)/commands.mk
