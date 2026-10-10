cmake_minimum_required(VERSION 3.16)

set(AI_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(AI_BSP_ROOT "${AI_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(AI_COMPONENT_ROOT "${AI_CUBE_ROOT}/Drivers/BSP/Components")
set(AI_ISP_ROOT "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp")

set(UAI_DRIVER_SOURCES
    c_bsp/stm32n6570_discovery_xspi.c
    board/register_diagnostics.cpp
    board/interrupt_priority.cpp
    board/time.cpp
    console_driver/console_driver.cpp
    console_driver/registers/console_registers.cpp
    cache_driver/cache_driver.cpp
    dma2d_driver/dma2d_driver.cpp
    camera_driver/board_driver/stm32n6570_discovery_camera.c
    camera_driver/camera_diagnostics.cpp
    camera_driver/camera_driver.cpp
    camera_driver/camera_controls.cpp
    camera_driver/sensor_driver/imx335_driver.c
    camera_driver/sensor_driver/registers/imx335_registers.cpp
    camera_driver/sensor_driver/registers/isp_controls.cpp
    lcd_driver/lcd_driver.cpp
    lcd_driver/display_driver.cpp
    lcd_driver/registers/lcd_registers.cpp
    nor_driver/nor_driver.cpp
    nor_driver/registers/nor_registers.cpp
    npu_driver/debug.cpp
    npu_driver/npu_driver.cpp
    npu_driver/registers/npu_registers.cpp
    psram_driver/psram_driver.cpp
    psram_driver/registers/psram_registers.cpp
    rif_driver/rif_driver.cpp
    touch_driver/touch_driver.cpp
    touch_driver/registers/touch_registers.cpp)
    if(APP_TARGET STREQUAL "hw-test")
        list(FILTER UAI_DRIVER_SOURCES EXCLUDE REGEX "^npu_driver/")
        list(APPEND UAI_DRIVER_SOURCES peripheral_driver/peripheral_driver.cpp)
    endif()
list(TRANSFORM UAI_DRIVER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(APPEND UAI_DRIVER_SOURCES
    "${AI_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${AI_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${AI_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${AI_COMPONENT_ROOT}/aps256xx/aps256xx.c"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g/mx66uw1g45g.c"
    "${AI_ISP_ROOT}/Src/isp_core.c"
    "${AI_ISP_ROOT}/Src/isp_algo.c"
    "${AI_ISP_ROOT}/Src/isp_services.c"
    "${AI_BSP_ROOT}/stm32n6570_discovery_ts.c"
    "${AI_COMPONENT_ROOT}/gt911/gt911.c"
    "${AI_COMPONENT_ROOT}/gt911/gt911_reg.c")

set(_generated_dma2d_source
    "${CUBEMX_OUTPUT_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma2d.c")
set(_generated_hal_config "${CUBEMX_OUTPUT_DIR}/FSBL/Core/Inc/stm32n6xx_hal_conf.h")
set(_generated_dma2d_enabled OFF)
if(EXISTS "${_generated_hal_config}")
    file(STRINGS "${_generated_hal_config}" _generated_dma2d_enabled
        REGEX "^#define[ \t]+HAL_DMA2D_MODULE_ENABLED")
endif()
if(NOT _generated_dma2d_enabled OR NOT EXISTS "${_generated_dma2d_source}")
    list(APPEND UAI_DRIVER_SOURCES
        "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma2d.c")
endif()

if(APP_TARGET STREQUAL "hw-test")
    set(STEDGEAI_LIB_DIR "" CACHE PATH
        "STEdgeAI runtime root containing Inc/, Npu/, and Lib/")
    if(NOT STEDGEAI_LIB_DIR AND DEFINED ENV{STEDGEAI_LIB_DIR})
        set(STEDGEAI_LIB_DIR "$ENV{STEDGEAI_LIB_DIR}" CACHE PATH
            "STEdgeAI runtime root containing Inc/, Npu/, and Lib/" FORCE)
    endif()
    if(NOT STEDGEAI_LIB_DIR)
        file(GLOB _hwtest_stedgeai_candidates LIST_DIRECTORIES true
            "/opt/ST/STEdgeAI/*/Middlewares/ST/AI")
        foreach(_hwtest_candidate IN LISTS _hwtest_stedgeai_candidates)
            if(EXISTS "${_hwtest_candidate}/Npu/Devices/STM32N6xx/npu_cache.h"
               AND EXISTS "${_hwtest_candidate}/Npu/Devices/STM32N6xx/npu_cache.c")
                set(STEDGEAI_LIB_DIR "${_hwtest_candidate}" CACHE PATH
                    "STEdgeAI runtime root containing Inc/, Npu/, and Lib/" FORCE)
                break()
            endif()
        endforeach()
    endif()
    set(_hwtest_npu_cache_dir "${STEDGEAI_LIB_DIR}/Npu/Devices/STM32N6xx")
    if(NOT STEDGEAI_LIB_DIR OR NOT EXISTS "${_hwtest_npu_cache_dir}/npu_cache.h"
       OR NOT EXISTS "${_hwtest_npu_cache_dir}/npu_cache.c")
        message(FATAL_ERROR
            "hw-test requires STEdgeAI NPU cache sources. Install STEdgeAI and configure "
            "with -DSTEDGEAI_LIB_DIR=/path/to/Middlewares/ST/AI (containing "
            "Npu/Devices/STM32N6xx/npu_cache.h and npu_cache.c).")
    endif()
    list(APPEND UAI_DRIVER_SOURCES "${_hwtest_npu_cache_dir}/npu_cache.c")
    set_source_files_properties("${_hwtest_npu_cache_dir}/npu_cache.c" PROPERTIES
        COMPILE_DEFINITIONS
        "NPU_CACHE_EXTERNAL_HAL_MSP;HAL_CACHEAXI_MspInit=hwtest_STEDGEAI_HAL_CACHEAXI_MspInit;HAL_CACHEAXI_MspDeInit=hwtest_STEDGEAI_HAL_CACHEAXI_MspDeInit")
    foreach(module dcmipp rng hash crc crc_ex rif dma dma_ex rtc rtc_ex tim tim_ex ltdc cacheaxi)
        string(REGEX REPLACE "_ex$" "" _base_module "${module}")
        string(TOUPPER "${_base_module}" _base_module)
        set(_module_enabled OFF)
        if(EXISTS "${_generated_hal_config}")
            file(STRINGS "${_generated_hal_config}" _module_enabled
                REGEX "^#define[ \t]+HAL_${_base_module}_MODULE_ENABLED")
        endif()
        set(_generated_hal_source
            "${CUBEMX_OUTPUT_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_${module}.c")
        if(NOT _module_enabled OR NOT EXISTS "${_generated_hal_source}")
            list(APPEND UAI_DRIVER_SOURCES
                "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_${module}.c")
        endif()
    endforeach()
endif()

set(AI_CAMERA_BSP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/camera_driver/board_driver/stm32n6570_discovery_camera.c")
set_source_files_properties(${AI_CAMERA_BSP_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=sample_ai_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=sample_ai_HAL_DCMIPP_PIPE_FrameEventCallback")

add_library(uai-drivers STATIC ${UAI_DRIVER_SOURCES})
add_library(uai::drivers ALIAS uai-drivers)

if(APP_TARGET STREQUAL "hw-test")
    target_include_directories(uai-drivers BEFORE PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/peripheral_driver/include")
    target_include_directories(uai-drivers PRIVATE
        "${STEDGEAI_LIB_DIR}/Inc"
        "${STEDGEAI_LIB_DIR}/Npu/ll_aton"
        "${_hwtest_npu_cache_dir}")
endif()

target_include_directories(uai-drivers
    PUBLIC
        "${PROJECT_SOURCE_DIR}/kernel"
        "${PROJECT_SOURCE_DIR}/kernel/middleware"
        "${UAI_GENERATED_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CUBEMX_OUTPUT_DIR}/Appli/Core/Inc"
        "${CUBEMX_OUTPUT_DIR}/FSBL/Core/Inc"
        "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
        "${AI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${AI_CUBE_ROOT}/Drivers/CMSIS/Include"
        "${AI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include"
        "${AI_BSP_ROOT}"
        "${AI_COMPONENT_ROOT}/Common"
        "${AI_COMPONENT_ROOT}/imx335"
        "${AI_COMPONENT_ROOT}/gt911"
        "${AI_COMPONENT_ROOT}/mx66uw1g45g"
        "${AI_COMPONENT_ROOT}/aps256xx"
        "${AI_ISP_ROOT}/Inc"
        "${AI_ISP_ROOT}/../evision/Inc"
        "${AI_ISP_ROOT}/../evision/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/board/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/c_bsp"
    PRIVATE
        "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc")

target_compile_definitions(uai-drivers PUBLIC
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    STM32N657xx
    USE_HAL_DRIVER
    USE_FULL_LL_DRIVER)
target_compile_definitions(uai-drivers PRIVATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1)
target_compile_options(uai-drivers PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(uai-drivers PUBLIC
    uai::utkernel
    uai::middleware)

# These strong HAL overrides must be linked directly into the final image so
# they win over weak HAL callbacks. Compile them as a kernel object target so
# they do not inherit userspace application include paths.
add_library(uai-driver-overrides OBJECT
    board/hal_time.c
    camera_driver/dcmipp_driver/dcmipp_callbacks.c)
add_library(uai::driver_overrides ALIAS uai-driver-overrides)
target_link_libraries(uai-driver-overrides PRIVATE uai::drivers)
target_compile_options(uai-driver-overrides PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
