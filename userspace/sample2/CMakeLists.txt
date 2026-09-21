cmake_minimum_required(VERSION 3.16)

# sample2 is intentionally self-contained.  It does not add files to the
# CubeMX tree or to the kernel; the extra HAL/BSP sources are selected here
# only when APP_TARGET=sample2 is configured.
set(SAMPLE2_MODEL "model1" CACHE STRING
    "Generated Neural-ART model to run (model1 or model2)")
set_property(CACHE SAMPLE2_MODEL PROPERTY STRINGS model1 model2)

if(NOT STM32CUBE_N6_DIR AND DEFINED ENV{STM32CUBE_N6_DIR})
    set(STM32CUBE_N6_DIR "$ENV{STM32CUBE_N6_DIR}" CACHE PATH
        "STM32CubeN6 package root" FORCE)
endif()
if(NOT STEDGEAI_LIB_DIR AND DEFINED ENV{STEDGEAI_LIB_DIR})
    set(STEDGEAI_LIB_DIR "$ENV{STEDGEAI_LIB_DIR}" CACHE PATH
        "STEdgeAI runtime package root" FORCE)
endif()

if(NOT STM32CUBE_N6_DIR)
    message(FATAL_ERROR
        "sample2 requires STM32CUBE_N6_DIR (the STM32CubeN6 package root).")
endif()
if(NOT STEDGEAI_LIB_DIR)
    message(FATAL_ERROR
        "sample2 requires STEDGEAI_LIB_DIR (the stedgeai-lib directory).")
endif()

set(SAMPLE2_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(SAMPLE2_STEDGEAI_ROOT "${STEDGEAI_LIB_DIR}")
set(SAMPLE2_BSP_ROOT
    "${SAMPLE2_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(SAMPLE2_COMPONENT_ROOT
    "${SAMPLE2_CUBE_ROOT}/Drivers/BSP/Components")
if(NOT DEFINED SAMPLE2_MODEL_DIR OR SAMPLE2_MODEL_DIR STREQUAL "")
    set(SAMPLE2_MODEL_DIR
        "${CMAKE_CURRENT_LIST_DIR}/models/${SAMPLE2_MODEL}")
endif()
set(SAMPLE2_RUNTIME_LIBRARY
    "${SAMPLE2_STEDGEAI_ROOT}/Lib/GCC/ARMCortexM55/NetworkRuntime1200_CM55_GCC.a")

set(SAMPLE2_RUNTIME_SOURCES
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/Devices/STM32N6xx/mcu_cache.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/Devices/STM32N6xx/npu_cache.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_rt_main.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_runtime.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_cipher.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_stai_internal.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_util.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_sw_float.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_sw_integer.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_lib.c"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton/ll_aton_lib_sw_operators.c")

set(SAMPLE2_BSP_SOURCES
    "${SAMPLE2_BSP_ROOT}/stm32n6570_discovery_xspi.c"
    "${SAMPLE2_COMPONENT_ROOT}/mx66uw1g45g/mx66uw1g45g.c"
    "${SAMPLE2_COMPONENT_ROOT}/aps256xx/aps256xx.c")

set(SAMPLE2_MODEL_SOURCES
    "${SAMPLE2_MODEL_DIR}/network.c"
    "${SAMPLE2_MODEL_DIR}/stai_network.c")
set(SAMPLE2_MODEL_HEADERS
    "${SAMPLE2_MODEL_DIR}/network_ecblobs.h"
    "${SAMPLE2_MODEL_DIR}/stai_network.h")
set(SAMPLE2_MODEL_WEIGHTS
    "${SAMPLE2_MODEL_DIR}/network_data.xSPI2.bin"
    "${SAMPLE2_MODEL_DIR}/network_data.hex")

set(SAMPLE2_REQUIRED_FILES
    "${SAMPLE2_RUNTIME_LIBRARY}"
    ${SAMPLE2_RUNTIME_SOURCES}
    ${SAMPLE2_BSP_SOURCES}
    ${SAMPLE2_MODEL_SOURCES}
    ${SAMPLE2_MODEL_HEADERS}
    ${SAMPLE2_MODEL_WEIGHTS})
foreach(_sample2_required IN LISTS SAMPLE2_REQUIRED_FILES)
    if(NOT EXISTS "${_sample2_required}")
        message(FATAL_ERROR
            "sample2 dependency is missing: ${_sample2_required}\n"
            "See userspace/sample2/README.md and run the model-generation "
            "script after installing STEdgeAI.")
    endif()
endforeach()

# The existing sample0 CubeMX output compiles the common HAL sources.  These
# modules are disabled in its HAL configuration, so enable them for this
# target only.  This does not edit the generated CubeMX files.
target_compile_definitions(stm32n6570_dk PUBLIC
    HAL_CACHEAXI_MODULE_ENABLED
    HAL_RAMCFG_MODULE_ENABLED
    HAL_RIF_MODULE_ENABLED)
target_include_directories(stm32n6570_dk BEFORE PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SAMPLE2_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/Include")

set(SAMPLE2_SOURCES
    src/main.cpp
    ${SAMPLE2_MODEL_SOURCES}
    ${SAMPLE2_RUNTIME_SOURCES}
    ${SAMPLE2_BSP_SOURCES})

add_executable(sample2.elf ${SAMPLE2_SOURCES})
target_include_directories(sample2.elf PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SAMPLE2_MODEL_DIR}"
    "${SAMPLE2_STEDGEAI_ROOT}/Inc"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/ll_aton"
    "${SAMPLE2_STEDGEAI_ROOT}/Npu/Devices/STM32N6xx"
    "${SAMPLE2_BSP_ROOT}"
    "${SAMPLE2_COMPONENT_ROOT}/mx66uw1g45g"
    "${SAMPLE2_COMPONENT_ROOT}/Common")
target_compile_definitions(sample2.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE
    HAL_CACHEAXI_MODULE_ENABLED
    HAL_RAMCFG_MODULE_ENABLED
    HAL_RIF_MODULE_ENABLED
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
    USE_RAM_MEMORY_APS256XX=1)
target_link_libraries(sample2.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${SAMPLE2_RUNTIME_LIBRARY}"
    m)

set(SAMPLE2_LINKER_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/stm32n6570-dk-npu-ram.ld")
target_link_options(sample2.elf PRIVATE
    "-T${SAMPLE2_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample2.map"
    "-Wl,--print-memory-usage")
set_target_properties(sample2.elf PROPERTIES
    LINK_DEPENDS "${SAMPLE2_LINKER_SCRIPT}")

add_custom_command(TARGET sample2.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:sample2.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/sample2.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:sample2.elf>"
    VERBATIM)

add_custom_target(sample2 DEPENDS sample2.elf)
