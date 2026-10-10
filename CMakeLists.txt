cmake_minimum_required(VERSION 3.16)

# Keep the application selection at the top level, like the existing
# ble-locator firmware.  Adding a new userspace application should not require
# changing the kernel project.
set(APP_TARGET "experiment-hello-world" CACHE STRING "Userspace application to build")
set_property(CACHE APP_TARGET PROPERTY STRINGS
    experiment-hello-world experiment-ai ai-app
    mini-ai-app experiment-model-load model-loader
    experiment-ui-control experiment-hw-test hw-test)

# ai-app's RAM image has a fixed launch trampoline at 0x34062000. Keep the
# supporting kernel/Cube objects small enough that ordinary code and rodata
# stay below it. The time-critical ai-app and model targets set their own
# -O3 options below, so the size-oriented default only applies to targets that
# do not provide a target-specific optimization level.
if(APP_TARGET STREQUAL "ai-app" AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING
        "Default build type for the memory-constrained ai-app RAM image" FORCE)
endif()

# Applications built on the kernel middleware and drivers. They generate the
# memory contract that the kernel components include.
set(UAI_KERNEL_APPS ai-app mini-ai-app hw-test)
include("${CMAKE_CURRENT_SOURCE_DIR}/project-tools/cmake/camera_board.cmake")

option(CMAKE_EXPORT_COMPILE_COMMANDS
       "Generate compile_commands.json for IDEs"
       ON)

option(UAI_KERNEL_TRACE_HOOKS
       "Enable µT-Kernel/DS execution hooks (adds tracing overhead)"
       OFF)

option(UAI_CPU_TASK_MONITOR
       "Enable the ai-app CPU task monitor (uses dispatch/interrupt hooks)"
       OFF)

set(_app_dir "${CMAKE_CURRENT_SOURCE_DIR}/userspace/${APP_TARGET}")
if(NOT EXISTS "${_app_dir}/CMakeLists.txt")
    message(FATAL_ERROR "Unknown APP_TARGET=${APP_TARGET}")
endif()

# This include must happen before project() so CMake uses the cross compiler
# during compiler detection, without requiring a separate Makefile.
include("${CMAKE_CURRENT_SOURCE_DIR}/project-tools/cmake/toolchain.cmake")

project(uai_studio LANGUAGES C CXX ASM)

include("${CMAKE_CURRENT_SOURCE_DIR}/project-tools/cmake/stm32_cli.cmake")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(kernel/utkernel)
if(UAI_KERNEL_TRACE_HOOKS OR UAI_CPU_TASK_MONITOR)
    target_compile_definitions(utkernel PUBLIC USE_DBGSPT_TRACE=1)
endif()
if(UAI_CAMERA_BOARD_APP)
    target_include_directories(utkernel PRIVATE
        "${UAI_CAMERA_BOARD_INCLUDE_DIR}"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Include")
    target_compile_definitions(utkernel PRIVATE
        STM32N6570_DK
        STM32N6570_DK_DEVELOPMENT_MODE
        USE_HAL_DRIVER
        STM32N657xx
        USE_FULL_LL_DRIVER
        CPU_IN_SECURE_STATE)
endif()
add_subdirectory(kernel/pre_kernel/stm32n6570-dk)
if(APP_TARGET IN_LIST UAI_KERNEL_APPS)
    # Generated memory contracts are build outputs shared by the middleware,
    # drivers, and this userspace application.
    set(UAI_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
    add_subdirectory(kernel/middleware)
    add_subdirectory(kernel/driver)
endif()
add_subdirectory("userspace/${APP_TARGET}")

if(UAI_CPU_TASK_MONITOR AND TARGET ${APP_TARGET}.elf)
    target_compile_definitions(${APP_TARGET}.elf PRIVATE UAI_CPU_TASK_MONITOR=1)
endif()
if(UAI_CPU_TASK_MONITOR AND TARGET uai-middleware)
    target_compile_definitions(uai-middleware PUBLIC UAI_CPU_TASK_MONITOR=1)
endif()

uai_add_stm32_cli_targets("${APP_TARGET}")
