cmake_minimum_required(VERSION 3.16)

set(AI_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(AI_BSP_ROOT "${AI_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(AI_COMPONENT_ROOT "${AI_CUBE_ROOT}/Drivers/BSP/Components")
set(AI_ISP_ROOT "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp")

set(UAI_DRIVER_SOURCES
    c_bsp/stm32n6570_discovery_xspi.c
    board/register_diagnostics.cpp
    board/interrupt_priority.cpp
    driver_ownership.cpp
    cache_driver/cache_driver.cpp
    camera_driver/board_driver/stm32n6570_discovery_camera.c
    camera_driver/camera_diagnostics.cpp
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
list(TRANSFORM UAI_DRIVER_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set(AI_CAMERA_BSP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/camera_driver/board_driver/stm32n6570_discovery_camera.c")
set_source_files_properties(${AI_CAMERA_BSP_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=sample_ai_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=sample_ai_HAL_DCMIPP_PIPE_FrameEventCallback")

add_library(uai-drivers STATIC ${UAI_DRIVER_SOURCES})
add_library(uai::drivers ALIAS uai-drivers)

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
        "${AI_COMPONENT_ROOT}/mx66uw1g45g"
        "${AI_ISP_ROOT}/Inc"
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
