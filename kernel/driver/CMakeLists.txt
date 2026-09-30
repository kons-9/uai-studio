cmake_minimum_required(VERSION 3.16)

if(NOT APP_TARGET STREQUAL "ai-app")
    message(FATAL_ERROR "kernel/driver is currently used by ai-app only")
endif()

set(AI_APP_SOURCE_DIR "${PROJECT_SOURCE_DIR}/userspace/ai-app/src")
set(AI_APP_CONFIG_DIR "${PROJECT_SOURCE_DIR}/userspace/ai-app/config")
set(AI_MEMORY_LAYOUT_GENERATED_DIR "${PROJECT_SOURCE_DIR}/userspace/ai-app/generated")
set(AI_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(AI_BSP_ROOT "${AI_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(AI_COMPONENT_ROOT "${AI_CUBE_ROOT}/Drivers/BSP/Components")
set(AI_ISP_ROOT "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp")

set(AI_APP_DRIVER_SOURCES
    c_bsp/stm32n6570_discovery_xspi.c
    driver_ownership.cpp
    cache_driver/cache_driver.cpp
    camera_driver/board_driver/stm32n6570_discovery_camera.c
    camera_driver/camera_driver.cpp
    camera_driver/sensor_driver/imx335_driver.c
    camera_driver/sensor_driver/registers/imx335_registers.cpp
    lcd_driver/lcd_driver.cpp
    lcd_driver/registers/lcd_registers.cpp
    nor_driver/nor_driver.cpp
    nor_driver/registers/nor_registers.cpp
    npu_driver/debug.cpp
    npu_driver/npu_driver.cpp
    npu_driver/registers/npu_registers.cpp
    psram_driver/psram_driver.cpp
    psram_driver/registers/psram_registers.cpp
    rif_driver/rif_driver.cpp)
list(TRANSFORM AI_APP_DRIVER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set(AI_CAMERA_BSP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/camera_driver/board_driver/stm32n6570_discovery_camera.c")
set_source_files_properties(${AI_CAMERA_BSP_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=sample_ai_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=sample_ai_HAL_DCMIPP_PIPE_FrameEventCallback")

add_library(ai-app-drivers STATIC ${AI_APP_DRIVER_SOURCES})
add_library(uai::ai_app_drivers ALIAS ai-app-drivers)

target_include_directories(ai-app-drivers
    PUBLIC
        "${PROJECT_SOURCE_DIR}/kernel"
        "${PROJECT_SOURCE_DIR}/kernel/middleware"
        "${AI_APP_SOURCE_DIR}"
        "${AI_APP_CONFIG_DIR}"
        "${AI_MEMORY_LAYOUT_GENERATED_DIR}"
        "${CUBEMX_OUTPUT_DIR}/Appli/Core/Inc"
        "${CUBEMX_OUTPUT_DIR}/FSBL/Core/Inc"
        "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
        "${AI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${AI_CUBE_ROOT}/Drivers/CMSIS/Include"
        "${AI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include"
        "${AI_BSP_ROOT}"
        "${AI_COMPONENT_ROOT}/Common"
        "${AI_COMPONENT_ROOT}/imx335"
        "${AI_COMPONENT_ROOT}/mx66uw1g45g"
        "${AI_ISP_ROOT}/Inc"
        "${AI_ISP_ROOT}/../evision/Inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/board/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/c_bsp"
    PRIVATE
        "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc")

target_compile_definitions(ai-app-drivers PUBLIC
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    STM32N657xx
    USE_HAL_DRIVER
    USE_FULL_LL_DRIVER)
target_compile_definitions(ai-app-drivers PRIVATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1)
target_compile_options(ai-app-drivers PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(ai-app-drivers PUBLIC
    uai::utkernel
    uai::ai_app_middleware)
