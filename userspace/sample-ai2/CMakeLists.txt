cmake_minimum_required(VERSION 3.16)

set(AI_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(AI_APP_TARGET sample-ai2.elf)
set(AI_APP_NAME sample-ai2)

# STEdgeAI's Neural-ART runtime is distributed separately from this sample.
# Do not silently depend on the old, ignored copy under third_party: that made
# a clean checkout appear buildable only on the developer machine that had
# populated it.  Prefer the explicitly supplied path, then discover the
# standard installation used by the ST tools.
set(STEDGEAI_LIB_DIR "" CACHE PATH
    "STEdgeAI runtime root containing Inc/, Npu/, and Lib/")
if(NOT STEDGEAI_LIB_DIR AND DEFINED ENV{STEDGEAI_LIB_DIR})
    set(STEDGEAI_LIB_DIR "$ENV{STEDGEAI_LIB_DIR}" CACHE PATH
        "STEdgeAI runtime root containing Inc/, Npu/, and Lib/" FORCE)
endif()

if(NOT STEDGEAI_LIB_DIR)
    set(_ai_stedgeai_candidates
        "/opt/ST/STEdgeAI/4.0/Middlewares/ST/AI")
    file(GLOB _ai_installed_stedgeai_candidates LIST_DIRECTORIES true
        "/opt/ST/STEdgeAI/*/Middlewares/ST/AI")
    list(APPEND _ai_stedgeai_candidates ${_ai_installed_stedgeai_candidates})
    foreach(_ai_candidate IN LISTS _ai_stedgeai_candidates)
        if(EXISTS "${_ai_candidate}/Npu/ll_aton/ll_aton_version.h")
            set(STEDGEAI_LIB_DIR "${_ai_candidate}" CACHE PATH
                "STEdgeAI runtime root containing Inc/, Npu/, and Lib/" FORCE)
            break()
        endif()
    endforeach()
endif()

if(NOT STEDGEAI_LIB_DIR OR
   NOT EXISTS "${STEDGEAI_LIB_DIR}/Npu/ll_aton/ll_aton_version.h")
    message(FATAL_ERROR
        "STEDGEAI_LIB_DIR is not configured. Install STEdgeAI and configure "
        "with -DSTEDGEAI_LIB_DIR=/path/to/Middlewares/ST/AI. The runtime is "
        "not vendored in this repository.")
endif()

set(AI_STEDGEAI_DIR "${STEDGEAI_LIB_DIR}")
set(AI_VISION_MODELS_PP_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vision_models_pp" CACHE PATH
    "ST vision-model post-processing sources used by sample-ai2")

set(AI_STEDGEAI_RUNTIME_LIBRARY "" CACHE FILEPATH
    "STEdgeAI CM55 GCC runtime archive (optional; auto-detected when empty)")
if(NOT AI_STEDGEAI_RUNTIME_LIBRARY)
    foreach(_ai_runtime_name
            NetworkRuntime1201_CM55_GCC.a
            NetworkRuntime1200_CM55_GCC.a)
        if(EXISTS "${AI_STEDGEAI_DIR}/Lib/GCC/ARMCortexM55/${_ai_runtime_name}")
            set(AI_STEDGEAI_RUNTIME_LIBRARY
                "${AI_STEDGEAI_DIR}/Lib/GCC/ARMCortexM55/${_ai_runtime_name}"
                CACHE FILEPATH
                "STEdgeAI CM55 GCC runtime archive (optional; auto-detected when empty)"
                FORCE)
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${AI_STEDGEAI_RUNTIME_LIBRARY}")
    message(FATAL_ERROR
        "No CM55 GCC STEdgeAI runtime archive was found below "
        "${AI_STEDGEAI_DIR}/Lib/GCC/ARMCortexM55. Expected "
        "NetworkRuntime1201_CM55_GCC.a or NetworkRuntime1200_CM55_GCC.a.")
endif()

set(_ai_required_runtime_paths
    "${AI_STEDGEAI_DIR}/Inc"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton"
    "${AI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx")
foreach(_ai_runtime_path IN LISTS _ai_required_runtime_paths)
    if(NOT EXISTS "${_ai_runtime_path}")
        message(FATAL_ERROR "STEdgeAI runtime path is missing: ${_ai_runtime_path}")
    endif()
endforeach()

set(_ai_required_postprocess_paths
    "${AI_VISION_MODELS_PP_DIR}/Inc/od_st_yolox_pp_if.h"
    "${AI_VISION_MODELS_PP_DIR}/Inc/fd_blazeface_pp_if.h"
    "${AI_VISION_MODELS_PP_DIR}/Src/od_pp_st_yolox.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/fd_pp_blazeface.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp_maxi_is8.c")
foreach(_ai_postprocess_path IN LISTS _ai_required_postprocess_paths)
    if(NOT EXISTS "${_ai_postprocess_path}")
        message(FATAL_ERROR
            "sample-ai2 post-processing dependency is missing: "
            "${_ai_postprocess_path}. Run the dependency setup described in "
            "userspace/sample-ai2/README.md.")
    endif()
endforeach()

# Generated model sources are intentionally not tracked.  Fail at configure
# time with the regeneration command instead of producing a cryptic include
# or file(READ) error in a clean checkout.
set(_ai_generated_model_files
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person/network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person/network_ecblobs.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person/stai_network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person/stai_network.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/segmentation/network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/segmentation/network_ecblobs.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/segmentation/stai_network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/segmentation/stai_network.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/face/network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/face/network_ecblobs.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/face/stai_network.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/face/stai_network.h")
foreach(_ai_generated_model_file IN LISTS _ai_generated_model_files)
    if(NOT EXISTS "${_ai_generated_model_file}")
        message(FATAL_ERROR
            "Generated model file is missing: ${_ai_generated_model_file}. "
            "Run userspace/sample-ai2/models/generate_model.sh for person, "
            "segmentation, and face before configuring sample-ai2.")
    endif()
endforeach()

# Generated network.c files contain the exact ll_aton development version they
# were produced with.  Fail at configure time instead of allowing a seemingly
# successful build with a mismatched runtime to fail on the board.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/models/person/network.c" _ai_model_c)
file(READ "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_version.h" _ai_runtime_version_h)
string(REGEX MATCH
    "LL_ATON_VERSION_DEV[ \t]*!=[ \t]*\\(([0-9]+)\\)"
    _ai_model_version_match "${_ai_model_c}")
string(REGEX MATCH
    "#define LL_ATON_VERSION_DEV[ \t]*\\(([0-9]+)\\)"
    _ai_runtime_version_match "${_ai_runtime_version_h}")
if(_ai_model_version_match AND _ai_runtime_version_match AND
   NOT "${CMAKE_MATCH_1}" STREQUAL "")
    set(_ai_runtime_dev "${CMAKE_MATCH_1}")
    string(REGEX REPLACE
        ".*LL_ATON_VERSION_DEV[ \t]*!=[ \t]*\\(([0-9]+)\\).*"
        "\\1" _ai_model_dev "${_ai_model_version_match}")
    if(NOT "${_ai_model_dev}" STREQUAL "${_ai_runtime_dev}")
        message(FATAL_ERROR
            "STEdgeAI runtime version mismatch: model expects ll_aton dev "
            "${_ai_model_dev}, but ${AI_STEDGEAI_DIR} provides dev "
            "${_ai_runtime_dev}. Use the same STEdgeAI release used to "
            "generate models.")
    endif()
endif()

set(AI_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(AI_BSP_ROOT
    "${AI_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(AI_COMPONENT_ROOT
    "${AI_CUBE_ROOT}/Drivers/BSP/Components")

# Keep the sample application explicitly optimized even though the common
# Cortex-M55 toolchain also defaults to -O3. Do not use -Ofast here: its
# relaxed floating-point rules can change post-processing results and
# detection thresholds.
set(AI_PERFORMANCE_COMPILE_OPTIONS
    -O3)

set(AI_MODEL_TARGET sample-ai2_models)
set(AI_MODEL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/segmentation"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/face")
set(AI_MODEL_C_API_SOURCES
    "${AI_SRC_DIR}/models/person/model_person.c"
    "${AI_SRC_DIR}/models/segmentation/model_segmentation.c"
    "${AI_SRC_DIR}/models/face/model_face.c")
set(AI_MODEL_SOURCES
    "${AI_SRC_DIR}/models/model.cpp"
    "${AI_SRC_DIR}/models/person/model.cpp"
    "${AI_SRC_DIR}/models/person/person_decoder.cpp"
    "${AI_SRC_DIR}/models/segmentation/model.cpp"
    "${AI_SRC_DIR}/models/segmentation/segmentation_decoder.cpp"
    "${AI_SRC_DIR}/models/face/model.cpp"
    "${AI_SRC_DIR}/models/face/face_decoder.cpp")
set(AI_MODEL_EXTRA_SOURCES
    "${AI_VISION_MODELS_PP_DIR}/Src/od_pp_st_yolox.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/fd_pp_blazeface.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp_maxi_is8.c"
    "${AI_VISION_MODELS_PP_DIR}/Src/vision_models_pp.c")
set(AI_MODEL_EXTRA_LIBRARIES
    "${AI_STEDGEAI_RUNTIME_LIBRARY}")
set(AI_NPU_CACHE_SOURCE
    "${AI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx/npu_cache.c")
# CubeMX already supplies the strong HAL cache MSP hooks. Some STEdgeAI
# packages also contain fallback hooks in npu_cache.c, so rename only those
# two functions in the package source and let CubeMX's hooks remain active.
set_source_files_properties(${AI_NPU_CACHE_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_CACHEAXI_MspInit=sample_ai_STEDGEAI_HAL_CACHEAXI_MspInit;HAL_CACHEAXI_MspDeInit=sample_ai_STEDGEAI_HAL_CACHEAXI_MspDeInit")
set(AI_CAMERA_BSP_SOURCE
    "${AI_SRC_DIR}/driver/camera_driver/board_driver/stm32n6570_discovery_camera.c")
set_source_files_properties(${AI_CAMERA_BSP_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=sample_ai_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=sample_ai_HAL_DCMIPP_PIPE_FrameEventCallback")

add_library(sample-ai2_middleware STATIC
    src/middleware/image_resizer/image_resizer.cpp)
target_include_directories(sample-ai2_middleware PUBLIC
    "${AI_SRC_DIR}/middleware")
target_compile_options(sample-ai2_middleware PRIVATE
    ${AI_PERFORMANCE_COMPILE_OPTIONS})

add_executable(${AI_APP_TARGET}
    src/main.cpp
    src/task/application_initialize_task.cpp
    src/task/camera_render_task.cpp
    src/task/inference_task.cpp
    src/task/input_preparation_task.cpp
    src/task/inference_postprocess_task.cpp
    src/task/task_context.cpp
    src/task/task_diagnostics.cpp
    src/driver/camera_driver/sensor_driver/registers/imx335_registers.cpp
    src/driver/camera_driver/dcmipp_resize.cpp
    src/driver/camera_driver/camera_driver.cpp
    src/driver/board/hal_time.c
    src/driver/cache_driver/cache_driver.cpp
    src/driver/nor_driver/registers/nor_registers.cpp
    src/driver/nor_driver/nor_driver.cpp
    src/driver/psram_driver/registers/psram_registers.cpp
    src/driver/psram_driver/psram_driver.cpp
    src/driver/rif_driver/rif_driver.cpp
    src/driver/lcd_driver/registers/lcd_registers.cpp
    src/driver/lcd_driver/lcd_driver.cpp
    src/memory_allocator/memory_allocator.cpp
    src/driver/npu_driver/registers/npu_registers.cpp
    src/driver/npu_driver/npu_driver.cpp
    src/driver/npu_driver/debug.cpp
    src/npu_runtime/npu_runtime.cpp
    src/npu_runtime/scheduler/scheduler.cpp
    src/npu_runtime/inference_dispatcher/inference_dispatcher.cpp
    src/npu_runtime/thread_monitor/thread_monitor.cpp
    ${AI_MODEL_SOURCES}
    # Project-owned ref-compatible BSP with a reset fallback for NOR left in
    # OPI-DTR by the external loader.
    "${AI_SRC_DIR}/driver/c_bsp/stm32n6570_discovery_xspi.c"
    "${AI_BSP_ROOT}/stm32n6570_discovery_bus.c"
    ${AI_CAMERA_BSP_SOURCE}
    src/driver/camera_driver/dcmipp_driver/dcmipp_callbacks.c
    src/driver/camera_driver/sensor_driver/imx335_driver.c
    "${AI_CUBE_ROOT}/Drivers/BSP/Components/imx335/imx335_reg.c"
    "${AI_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Src/isp_core.c"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Src/isp_algo.c"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Src/isp_services.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma2d.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc.c"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g/mx66uw1g45g.c"
    "${AI_COMPONENT_ROOT}/aps256xx/aps256xx.c")

target_include_directories(${AI_APP_TARGET} PRIVATE
    "${AI_SRC_DIR}"
    "${AI_SRC_DIR}/middleware"
    "${AI_SRC_DIR}/driver/board/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    ${AI_MODEL_INCLUDE_DIRS}
    "${AI_STEDGEAI_DIR}/Inc"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton"
    "${AI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx"
    "${AI_VISION_MODELS_PP_DIR}/Inc"
    "${AI_BSP_ROOT}"
    "${AI_COMPONENT_ROOT}/Common"
    "${AI_COMPONENT_ROOT}/imx335"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Inc"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Inc"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")

target_compile_definitions(${AI_APP_TARGET} PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
    NPU_CACHE_EXTERNAL_HAL_MSP)
target_compile_options(${AI_APP_TARGET} PRIVATE
    ${AI_PERFORMANCE_COMPILE_OPTIONS}
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(${AI_APP_TARGET} PRIVATE
    sample-ai2_middleware
    uai::utkernel
    uai::stm32n6570_dk
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Lib/libn6-evision-awb_gcc.a"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(AI_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/stm32n6570-dk-npu-ram.ld")
target_link_options(${AI_APP_TARGET} PRIVATE
    "-T${AI_LINKER_SCRIPT}"
    "-Wl,--undefined=NPU0_IRQHandler"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${AI_APP_NAME}.map"
    "-Wl,--print-memory-usage")
target_link_options(${AI_APP_TARGET} PRIVATE
    "-Wl,--section-start=.network_blobs_person=0x70500000"
    "-Wl,--section-start=.network_blobs_segmentation=0x70560000"
    "-Wl,--section-start=.network_blobs_face=0x70580000")
set_target_properties(${AI_APP_TARGET} PROPERTIES
    LINK_DEPENDS "${AI_LINKER_SCRIPT}")

add_custom_command(TARGET ${AI_APP_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            --remove-section=.network_blobs_person
            --remove-section=.network_blobs_segmentation
            --remove-section=.network_blobs_face
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/${AI_APP_NAME}.bin"
    COMMAND ${CMAKE_OBJCOPY} -O binary
            --only-section=.network_blobs_person
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_person.bin"
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            --only-section=.network_blobs_person
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_person.hex"
    COMMAND ${CMAKE_OBJCOPY} -O binary
            --only-section=.network_blobs_segmentation
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_segmentation.bin"
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            --only-section=.network_blobs_segmentation
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_segmentation.hex"
    COMMAND ${CMAKE_OBJCOPY} -O binary
            --only-section=.network_blobs_face
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_face.bin"
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            --only-section=.network_blobs_face
            "$<TARGET_FILE:${AI_APP_TARGET}>"
            "${CMAKE_CURRENT_BINARY_DIR}/network_blobs_face.hex"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${AI_APP_TARGET}>"
    VERBATIM)

add_custom_target(${AI_APP_NAME} DEPENDS ${AI_APP_TARGET})

# STM32N6 external-flash boot uses the CubeMX FSBL LRUN flow.  The signing
# tool adds the 0x400-byte image header expected by the FSBL; the payload is
# then copied to internal RAM before the application starts.
set(AI_FLASH_APP_IMAGE
    "${CMAKE_CURRENT_BINARY_DIR}/${AI_APP_NAME}-flash.bin")
set(AI_FLASH_FSBL_IMAGE
    "${CMAKE_CURRENT_SOURCE_DIR}/fsbl/stm32n6570-dk-ai_fsbl.hex")
if(NOT EXISTS "${AI_FLASH_FSBL_IMAGE}")
    message(FATAL_ERROR
        "The official sample-ai FSBL is missing: ${AI_FLASH_FSBL_IMAGE}")
endif()
if(STM32_SIGNING_TOOL_CLI)
    add_custom_command(
        OUTPUT "${AI_FLASH_APP_IMAGE}"
        COMMAND "${STM32_SIGNING_TOOL_CLI}"
                -bin "${CMAKE_CURRENT_BINARY_DIR}/${AI_APP_NAME}.bin"
                -nk -of 0x80000000 -t fsbl -o "${AI_FLASH_APP_IMAGE}"
                -hv 2.3 -align -s -dump "${AI_FLASH_APP_IMAGE}"
        DEPENDS ${AI_APP_TARGET}
        VERBATIM)
    add_custom_target(sample-ai2-flash-images
        DEPENDS "${AI_FLASH_APP_IMAGE}")
else()
    add_custom_target(sample-ai2-flash-images
        COMMAND ${CMAKE_COMMAND} -E echo
                "sample-ai2-flash-images requires STM32_SigningTool_CLI"
        COMMAND ${CMAKE_COMMAND} -E false)
endif()

add_library(${AI_MODEL_TARGET} STATIC
    ${AI_MODEL_C_API_SOURCES}
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ecloader.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_cipher.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_dbgtrc.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_lib.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_lib_sw_operators.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_runtime.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_stai_internal.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_aton_util.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_sw_float.c"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton/ll_sw_integer.c"
    "${AI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx/mcu_cache.c"
    ${AI_NPU_CACHE_SOURCE}
    ${AI_MODEL_EXTRA_SOURCES})

target_include_directories(${AI_MODEL_TARGET} PRIVATE
    "${AI_SRC_DIR}"
    "${AI_SRC_DIR}/middleware"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    ${AI_MODEL_INCLUDE_DIRS}
    "${AI_STEDGEAI_DIR}/Inc"
    "${AI_STEDGEAI_DIR}/Npu/ll_aton"
    "${AI_STEDGEAI_DIR}/Npu/Devices/STM32N6xx"
    "${AI_VISION_MODELS_PP_DIR}/Inc"
    "${AI_BSP_ROOT}"
    "${AI_COMPONENT_ROOT}/Common"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")
target_compile_definitions(${AI_MODEL_TARGET} PRIVATE
    __int64_t_defined=1
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
    NPU_CACHE_EXTERNAL_HAL_MSP)
# Export the vendor NPU handler under a private name for every model. The
# application wrapper calls it first, then wakes the inference task through a
# T-Kernel event flag. This avoids a fixed polling delay between epoch blocks.
target_compile_definitions(${AI_MODEL_TARGET} PRIVATE
    NPU0_IRQHandler=LL_ATON_NPU0_IRQHandler)
target_compile_options(${AI_MODEL_TARGET} PRIVATE
    ${AI_PERFORMANCE_COMPILE_OPTIONS}
    $<$<COMPILE_LANGUAGE:C>:-include;inttypes.h>
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(${AI_MODEL_TARGET} PRIVATE
    uai::utkernel
    uai::stm32n6570_dk)
target_link_libraries(${AI_APP_TARGET} PRIVATE
    ${AI_MODEL_TARGET}
    ${AI_MODEL_EXTRA_LIBRARIES})
add_custom_target(sample-ai2-models DEPENDS ${AI_MODEL_TARGET})
