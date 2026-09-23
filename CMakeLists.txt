cmake_minimum_required(VERSION 3.16)

# Keep the application selection at the top level, like the existing
# ble-locator firmware.  Adding a new userspace application should not require
# changing the kernel project.
set(APP_TARGET "sample0" CACHE STRING "Userspace application to build")
set_property(CACHE APP_TARGET PROPERTY STRINGS sample0 sample1)

option(CMAKE_EXPORT_COMPILE_COMMANDS
       "Generate compile_commands.json for IDEs"
       ON)

set(_app_dir "${CMAKE_CURRENT_SOURCE_DIR}/userspace/${APP_TARGET}")
if(NOT EXISTS "${_app_dir}/CMakeLists.txt")
    message(FATAL_ERROR "Unknown APP_TARGET=${APP_TARGET}")
endif()

# This include must happen before project() so CMake uses the cross compiler
# during compiler detection, without requiring a separate Makefile.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain.cmake")

project(uai_studio LANGUAGES C CXX ASM)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/stm32_cli.cmake")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(kernel/utkernel)
add_subdirectory(kernel/pre_kernel/stm32n6570-dk)
if(APP_TARGET STREQUAL "sample1" OR APP_TARGET STREQUAL "sample2")
    add_subdirectory(kernel/driver)
endif()
add_subdirectory("userspace/${APP_TARGET}")

uai_add_stm32_cli_targets("${APP_TARGET}")
