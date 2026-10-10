cmake_minimum_required(VERSION 3.16)

# mini-ai-app: one camera, one NPU model (person), boxes on the LCD.
# This is the tutorial application; ai-app is the full version.
set(MINI_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(MINI_APP_NAME mini-ai-app)
set(MINI_APP_TARGET ${MINI_APP_NAME}.elf)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ---------------------------------------------------------------------------
# External dependencies: STEdgeAI runtime (not vendored), ST vision
# post-processing sources, and the generated person network. The model
# generator and the post-processing sources are shared with ai-app.
# ---------------------------------------------------------------------------
set(STEDGEAI_LIB_DIR "" CACHE PATH
    "STEdgeAI runtime root containing Inc/, Npu/, and Lib/")
if(NOT STEDGEAI_LIB_DIR AND DEFINED ENV{STEDGEAI_LIB_DIR})
    set(STEDGEAI_LIB_DIR "$ENV{STEDGEAI_LIB_DIR}" CACHE PATH
        "STEdgeAI runtime root containing Inc/, Npu/, and Lib/" FORCE)
endif()
if(NOT STEDGEAI_LIB_DIR)
    file(GLOB _mini_stedgeai_candidates LIST_DIRECTORIES true
        "/opt/ST/STEdgeAI/*/Middlewares/ST/AI")
    foreach(_mini_candidate IN LISTS _mini_stedgeai_candidates)
        if(EXISTS "${_mini_candidate}/Npu/ll_aton/ll_aton_version.h")
            set(STEDGEAI_LIB_DIR "${_mini_candidate}" CACHE PATH
                "STEdgeAI runtime root containing Inc/, Npu/, and Lib/" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT STEDGEAI_LIB_DIR OR
   NOT EXISTS "${STEDGEAI_LIB_DIR}/Npu/ll_aton/ll_aton_version.h")
    message(FATAL_ERROR
        "STEDGEAI_LIB_DIR is not configured. Install STEdgeAI and configure "
        "with -DSTEDGEAI_LIB_DIR=/path/to/Middlewares/ST/AI.")
endif()
set(MINI_STEDGEAI_DIR "${STEDGEAI_LIB_DIR}")

set(MINI_AI_MODELS_DIR
    "${PROJECT_SOURCE_DIR}/userspace/ai-app/models" CACHE PATH
    "Directory holding <model>/ generated STEdgeAI artifacts")
set(AI_VISION_MODELS_PP_DIR
    "${PROJECT_SOURCE_DIR}/userspace/ai-app/third_party/vision_models_pp"
    CACHE PATH "ST vision-model post-processing sources")

set(MINI_STEDGEAI_RUNTIME_LIBRARY "")
foreach(_mini_runtime_name
        NetworkRuntime1201_CM55_GCC.a
        NetworkRuntime1200_CM55_GCC.a)
    if(EXISTS "${MINI_STEDGEAI_DIR}/Lib/GCC/ARMCortexM55/${_mini_runtime_name}")
        set(MINI_STEDGEAI_RUNTIME_LIBRARY
            "${MINI_STEDGEAI_DIR}/Lib/GCC/ARMCortexM55/${_mini_runtime_name}")
        break()
    endif()
endforeach()
if(NOT EXISTS "${MINI_STEDGEAI_RUNTIME_LIBRARY}")
    message(FATAL_ERROR
        "No CM55 GCC STEdgeAI runtime archive was found below "
        "${MINI_STEDGEAI_DIR}/Lib/GCC/ARMCortexM55.")
endif()

set(_mini_required_files
    "${AI_VISION_MODELS_PP_DIR}/Inc/od_st_yolox_pp_if.h"
    "${AI_VISION_MODELS_PP_DIR}/Src/od_pp_st_yolox.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp_maxi_is8.c"
    "${MINI_AI_MODELS_DIR}/person/network.c"
    "${MINI_AI_MODELS_DIR}/person/network_ecblobs.h"
    "${MINI_AI_MODELS_DIR}/person/stai_network.c"
    "${MINI_AI_MODELS_DIR}/person/stai_network.h"
    "${MINI_AI_MODELS_DIR}/person/network_data.xSPI2.bin")
foreach(_mini_file IN LISTS _mini_required_files)
    if(NOT EXISTS "${_mini_file}")
        message(FATAL_ERROR
            "mini-ai-app dependency is missing: ${_mini_file}. Run "
            "'make -C userspace/mini-ai-app setup' (ai-deps + ai-models).")
    endif()
endforeach()

# The generated network.c records the ll_aton version it was produced with.
file(READ "${MINI_AI_MODELS_DIR}/person/network.c" _mini_model_c)
file(READ "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_version.h" _mini_runtime_h)
string(REGEX MATCH "LL_ATON_VERSION_DEV[ \t]*!=[ \t]*\\(([0-9]+)\\)"
    _mini_model_version "${_mini_model_c}")
set(_mini_model_dev "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define LL_ATON_VERSION_DEV[ \t]*\\(([0-9]+)\\)"
    _mini_runtime_version "${_mini_runtime_h}")
set(_mini_runtime_dev "${CMAKE_MATCH_1}")
if(_mini_model_version AND _mini_runtime_version AND
   NOT "${_mini_model_dev}" STREQUAL "${_mini_runtime_dev}")
    message(FATAL_ERROR
        "STEdgeAI runtime version mismatch: model expects ll_aton dev "
        "${_mini_model_dev}, runtime provides dev ${_mini_runtime_dev}.")
endif()

set(MINI_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(MINI_BSP_ROOT "${MINI_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(MINI_COMPONENT_ROOT "${MINI_CUBE_ROOT}/Drivers/BSP/Components")

# ---------------------------------------------------------------------------
# Memory layout: config/*.json + generated model -> headers and linker script.
# The kernel middleware and drivers include the generated contract.
# ---------------------------------------------------------------------------
set(MINI_LAYOUT_GENERATOR_DIR
    "${PROJECT_SOURCE_DIR}/host_app/auto_static_memory_layout")
file(GLOB MINI_LAYOUT_GENERATOR_SOURCES "${MINI_LAYOUT_GENERATOR_DIR}/*.py")
set(MINI_LAYOUT_DIR "${UAI_GENERATED_INCLUDE_DIR}")
set(MINI_LAYOUT_OUTPUTS
    "${MINI_LAYOUT_DIR}/memory_layout.json"
    "${MINI_LAYOUT_DIR}/memory_layout.yml"
    "${MINI_LAYOUT_DIR}/middleware/memory/generated/static_memory_layout/raw.hpp"
    "${MINI_LAYOUT_DIR}/middleware/memory/generated/static_memory_layout/key.hpp"
    "${MINI_LAYOUT_DIR}/middleware/memory/generated/memory_config.hpp"
    "${MINI_LAYOUT_DIR}/stm32n6570-dk-npu-ram.ld")
set(MINI_LAYOUT_BASE_LINKER
    "${CMAKE_CURRENT_SOURCE_DIR}/stm32n6570-dk-npu-ram.ld")
add_custom_command(
    OUTPUT ${MINI_LAYOUT_OUTPUTS}
    COMMAND ${Python3_EXECUTABLE} "${MINI_LAYOUT_GENERATOR_DIR}" all
            --board "${CMAKE_CURRENT_SOURCE_DIR}/config/board_memory.json"
            --application "${CMAKE_CURRENT_SOURCE_DIR}/config/application_memory.json"
            --models-dir "${MINI_AI_MODELS_DIR}"
            --model-config "${CMAKE_CURRENT_SOURCE_DIR}/config/model_layout.json"
            --output-dir "${MINI_LAYOUT_DIR}"
            --linker-base "${MINI_LAYOUT_BASE_LINKER}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/config/board_memory.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/application_memory.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/model_layout.json"
        "${MINI_LAYOUT_BASE_LINKER}"
        ${MINI_LAYOUT_GENERATOR_SOURCES}
        "${MINI_AI_MODELS_DIR}/person/stai_network.h"
        "${MINI_AI_MODELS_DIR}/person/network_ecblobs.h"
        "${MINI_AI_MODELS_DIR}/person/network_data.xSPI2.bin"
    VERBATIM)
add_custom_target(${MINI_APP_NAME}-memory-layout DEPENDS ${MINI_LAYOUT_OUTPUTS})
add_dependencies(uai-memory-layout ${MINI_APP_NAME}-memory-layout)
add_dependencies(uai-middleware ${MINI_APP_NAME}-memory-layout)
add_dependencies(uai-drivers ${MINI_APP_NAME}-memory-layout)
add_dependencies(uai-driver-overrides ${MINI_APP_NAME}-memory-layout)
target_include_directories(uai-drivers PUBLIC
    "${MINI_STEDGEAI_DIR}/Inc"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton"
    "${MINI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx")

# ---------------------------------------------------------------------------
# Common include paths and definitions shared by the application and the
# generated-model library.
# ---------------------------------------------------------------------------
set(MINI_INCLUDE_DIRS
    "${MINI_LAYOUT_DIR}"
    "${MINI_SRC_DIR}"
    "${PROJECT_SOURCE_DIR}/kernel"
    "${PROJECT_SOURCE_DIR}/kernel/middleware"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${MINI_AI_MODELS_DIR}/person"
    "${MINI_STEDGEAI_DIR}/Inc"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton"
    "${MINI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx"
    "${AI_VISION_MODELS_PP_DIR}/Inc"
    "${MINI_BSP_ROOT}"
    "${MINI_COMPONENT_ROOT}/Common"
    "${MINI_COMPONENT_ROOT}/imx335"
    "${MINI_COMPONENT_ROOT}/mx66uw1g45g"
    "${MINI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Inc"
    "${MINI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Inc"
    "${MINI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${MINI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${MINI_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${MINI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")
set(MINI_ATON_DEFINITIONS
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
    NPU_CACHE_EXTERNAL_HAL_MSP)
# -Ofast is intentionally avoided: relaxed floating point changes the
# post-processing thresholds.
set(MINI_COMPILE_OPTIONS -O3)

# ---------------------------------------------------------------------------
# Generated model + Neural-ART runtime library.
# ---------------------------------------------------------------------------
set(MINI_MODEL_TARGET ${MINI_APP_NAME}_models)
set(MINI_NPU_CACHE_SOURCE
    "${MINI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx/npu_cache.c")
# CubeMX supplies the strong HAL cache MSP hooks; hide the package fallbacks.
set_source_files_properties(${MINI_NPU_CACHE_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_CACHEAXI_MspInit=mini_ai_STEDGEAI_HAL_CACHEAXI_MspInit;HAL_CACHEAXI_MspDeInit=mini_ai_STEDGEAI_HAL_CACHEAXI_MspDeInit")
add_library(${MINI_MODEL_TARGET} STATIC
    "${MINI_SRC_DIR}/model/person_network.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ecloader.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_cipher.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_dbgtrc.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_lib.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_lib_sw_operators.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_runtime.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_stai_internal.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_util.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_sw_float.c"
    "${MINI_STEDGEAI_DIR}/Npu/ll_aton/ll_sw_integer.c"
    "${MINI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx/mcu_cache.c"
    ${MINI_NPU_CACHE_SOURCE}
    "${AI_VISION_MODELS_PP_DIR}/Src/od_pp_st_yolox.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp_maxi_is8.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp.c")
target_include_directories(${MINI_MODEL_TARGET} PRIVATE ${MINI_INCLUDE_DIRS})
target_compile_definitions(${MINI_MODEL_TARGET} PRIVATE
    __int64_t_defined=1
    ${MINI_ATON_DEFINITIONS}
    # The vendor NPU handler is exported under a private name; the NPU driver
    # wraps it and wakes the waiting task through a µT-Kernel event flag.
    NPU0_IRQHandler=LL_ATON_NPU0_IRQHandler)
target_compile_options(${MINI_MODEL_TARGET} PRIVATE
    ${MINI_COMPILE_OPTIONS}
    $<$<COMPILE_LANGUAGE:C>:-include;inttypes.h>
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(${MINI_MODEL_TARGET} PRIVATE
    uai::utkernel
    uai::drivers
    uai::stm32n6570_dk)
add_dependencies(${MINI_MODEL_TARGET} ${MINI_APP_NAME}-memory-layout)

# ---------------------------------------------------------------------------
# Application.
# ---------------------------------------------------------------------------
add_executable(${MINI_APP_TARGET}
    src/main.cpp
    src/task/initialize_task.cpp
    src/task/camera_task.cpp
    src/task/inference_task.cpp
    src/model/person_model.cpp
    src/model/person_decoder.cpp
    # Keep the strong kernel HAL overrides directly in the executable.
    $<TARGET_OBJECTS:uai-driver-overrides>
    "${MINI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c"
    "${MINI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c"
    "${MINI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c"
    "${MINI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc.c")
target_include_directories(${MINI_APP_TARGET} PRIVATE ${MINI_INCLUDE_DIRS})
add_dependencies(${MINI_APP_TARGET} ${MINI_APP_NAME}-memory-layout)
target_compile_definitions(${MINI_APP_TARGET} PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    ${MINI_ATON_DEFINITIONS})
target_compile_options(${MINI_APP_TARGET} PRIVATE
    ${MINI_COMPILE_OPTIONS}
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(${MINI_APP_TARGET} PRIVATE
    ${MINI_MODEL_TARGET}
    "${MINI_STEDGEAI_RUNTIME_LIBRARY}"
    uai::utkernel
    uai::drivers
    uai::stm32n6570_dk
    "${MINI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Lib/libn6-evision-awb_gcc.a"
    "${MINI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(MINI_LINKER_SCRIPT "${MINI_LAYOUT_DIR}/stm32n6570-dk-npu-ram.ld")
target_link_options(${MINI_APP_TARGET} PRIVATE
    "-T${MINI_LINKER_SCRIPT}"
    "-Wl,--undefined=NPU0_IRQHandler"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${MINI_APP_NAME}.map"
    "-Wl,--print-memory-usage")
set_target_properties(${MINI_APP_TARGET} PROPERTIES
    LINK_DEPENDS "${MINI_LINKER_SCRIPT}")

# The RAM image excludes the command blob, which lives in NOR and is written
# separately by `make ai-load`.
add_custom_command(TARGET ${MINI_APP_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            --remove-section=.network_blobs_person
            "$<TARGET_FILE:${MINI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/${MINI_APP_NAME}.bin"
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            --only-section=.network_blobs_person
            "$<TARGET_FILE:${MINI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_person.hex"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${MINI_APP_TARGET}>"
    VERBATIM)

add_custom_target(${MINI_APP_NAME} DEPENDS ${MINI_APP_TARGET})
