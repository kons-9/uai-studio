cmake_minimum_required(VERSION 3.16)

if(NOT STM32CUBE_N6_DIR)
    message(FATAL_ERROR
        "kernel/driver requires STM32CUBE_N6_DIR. Set it to the STM32CubeN6 package root.")
endif()

set(DRIVER_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(DRIVER_BSP_ROOT
    "${DRIVER_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(DRIVER_COMPONENT_ROOT
    "${DRIVER_CUBE_ROOT}/Drivers/BSP/Components")
set(DRIVER_ISP_ROOT
    "${DRIVER_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library")
set(DRIVER_EVISION_ROOT
    "${DRIVER_ISP_ROOT}/evision")
set(DRIVER_HAL_ROOT
    "${DRIVER_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver")
set(DRIVER_ARCH_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/src/arch/stm32n6570-dk")
# CubeN6 v1.3.0's camera BSP drives NRST_CAM before EN_CAM and enables GPIOO
# instead of GPIOC. Keep a project-owned copy with the corrected reset sequence.
set(DRIVER_CAMERA_BSP_SOURCE
    "${DRIVER_ARCH_ROOT}/bsp/stm32n6570_discovery_camera.c")
# Keep the IMX335 frequency and stream-start handling in the project-owned
# component so streaming begins only after the CSI receiver is ready.
set(DRIVER_IMX335_SOURCE "${DRIVER_ARCH_ROOT}/bsp/imx335.c")
set(DRIVER_ARCH_INCLUDE_ROOT
    "${DRIVER_ARCH_ROOT}/include")

set(DRIVER_REQUIRED_FILES
    "${DRIVER_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${DRIVER_CAMERA_BSP_SOURCE}"
    "${DRIVER_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${DRIVER_BSP_ROOT}/isp_param_conf.h"
    "${DRIVER_COMPONENT_ROOT}/Common/camera.h"
    "${DRIVER_COMPONENT_ROOT}/Common/lcd.h"
    "${DRIVER_IMX335_SOURCE}"
    "${DRIVER_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${DRIVER_ISP_ROOT}/isp/Src/isp_core.c"
    "${DRIVER_ISP_ROOT}/isp/Src/isp_algo.c"
    "${DRIVER_ISP_ROOT}/isp/Src/isp_services.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_ramcfg.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_rif.c"
    "${DRIVER_EVISION_ROOT}/Lib/libn6-evision-st-ae_gcc.a"
    "${DRIVER_EVISION_ROOT}/Lib/libn6-evision-awb_gcc.a")
foreach(_required_file IN LISTS DRIVER_REQUIRED_FILES)
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "kernel/driver dependency is missing: ${_required_file}")
    endif()
endforeach()

# The common kernel targets provide the board clocks, UART, external memory,
# and µT-Kernel. Add only the headers/definitions needed by the DK media BSP.
target_include_directories(stm32n6570_dk BEFORE PUBLIC
    "${DRIVER_ARCH_INCLUDE_ROOT}"
    "${DRIVER_HAL_ROOT}/Inc"
    "${DRIVER_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${DRIVER_CUBE_ROOT}/Drivers/CMSIS/Include")
target_include_directories(utkernel BEFORE PUBLIC
    "${DRIVER_ARCH_INCLUDE_ROOT}"
    "${DRIVER_HAL_ROOT}/Inc"
    "${DRIVER_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${DRIVER_CUBE_ROOT}/Drivers/CMSIS/Include")
target_compile_definitions(utkernel PUBLIC
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)

add_library(uai_driver STATIC
    src/camera_driver.cpp
    src/display_driver.cpp
    src/arch/stm32n6570-dk/board_media.cpp
    src/arch/stm32n6570-dk/camera_driver_arch.cpp
    src/arch/stm32n6570-dk/display_driver_arch.cpp
    "${DRIVER_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${DRIVER_CAMERA_BSP_SOURCE}"
    "${DRIVER_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${DRIVER_IMX335_SOURCE}"
    "${DRIVER_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${DRIVER_ISP_ROOT}/isp/Src/isp_core.c"
    "${DRIVER_ISP_ROOT}/isp/Src/isp_algo.c"
    "${DRIVER_ISP_ROOT}/isp/Src/isp_services.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_ramcfg.c"
    "${DRIVER_HAL_ROOT}/Src/stm32n6xx_hal_rif.c")

target_include_directories(uai_driver
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE
        "${DRIVER_ARCH_ROOT}"
        "${DRIVER_ARCH_INCLUDE_ROOT}"
        "${DRIVER_BSP_ROOT}"
        "${DRIVER_COMPONENT_ROOT}/Common"
        "${DRIVER_COMPONENT_ROOT}/imx335"
        "${DRIVER_COMPONENT_ROOT}/rk050hr18"
        "${DRIVER_ISP_ROOT}/isp/Inc"
        "${DRIVER_EVISION_ROOT}/Inc"
        "${DRIVER_HAL_ROOT}/Inc"
        "${DRIVER_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${DRIVER_CUBE_ROOT}/Drivers/CMSIS/Include")
target_compile_definitions(uai_driver PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)
# Keep the pre-kernel OBJECT library out of this archive. Linking it here
# changes the final application's object order and weak startup-hook
# resolution. Applications must link uai::stm32n6570_dk directly, as the
# original working sample1 did.
target_link_libraries(uai_driver PUBLIC
    uai::utkernel
    "${DRIVER_EVISION_ROOT}/Lib/libn6-evision-st-ae_gcc.a"
    "${DRIVER_EVISION_ROOT}/Lib/libn6-evision-awb_gcc.a")

add_library(uai::driver ALIAS uai_driver)
