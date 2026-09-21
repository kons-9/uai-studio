cmake_minimum_required(VERSION 3.16)

if(NOT STM32CUBE_N6_DIR)
    message(FATAL_ERROR
        "sample1 requires STM32CUBE_N6_DIR. Set it to the STM32CubeN6 package root.")
endif()

set(SAMPLE1_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(SAMPLE1_BSP_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(SAMPLE1_COMPONENT_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/BSP/Components")
set(SAMPLE1_ISP_ROOT
    "${SAMPLE1_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library")
set(SAMPLE1_EVISION_ROOT
    "${SAMPLE1_ISP_ROOT}/evision")
set(SAMPLE1_HAL_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver")

set(SAMPLE1_REQUIRED_FILES
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_camera.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${SAMPLE1_BSP_ROOT}/isp_param_conf.h"
    "${SAMPLE1_COMPONENT_ROOT}/Common/camera.h"
    "${SAMPLE1_COMPONENT_ROOT}/Common/lcd.h"
    "${SAMPLE1_COMPONENT_ROOT}/imx335/imx335.c"
    "${SAMPLE1_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_core.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_algo.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_services.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_ramcfg.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_rif.c"
    "${SAMPLE1_EVISION_ROOT}/Lib/libn6-evision-st-ae_gcc.a"
    "${SAMPLE1_EVISION_ROOT}/Lib/libn6-evision-awb_gcc.a")
foreach(_required_file IN LISTS SAMPLE1_REQUIRED_FILES)
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "sample1 dependency is missing: ${_required_file}")
    endif()
endforeach()

target_include_directories(stm32n6570_dk BEFORE PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SAMPLE1_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${SAMPLE1_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${SAMPLE1_CUBE_ROOT}/Drivers/CMSIS/Include")
target_include_directories(utkernel BEFORE PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${SAMPLE1_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${SAMPLE1_CUBE_ROOT}/Drivers/CMSIS/Include")
target_compile_definitions(utkernel PUBLIC
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)

set(SAMPLE1_SOURCES
    src/main.cpp
    src/camera_irqs.cpp
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_ramcfg.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_rif.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_camera.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${SAMPLE1_COMPONENT_ROOT}/imx335/imx335.c"
    "${SAMPLE1_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_core.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_algo.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_services.c")

add_executable(sample1.elf ${SAMPLE1_SOURCES})
target_include_directories(sample1.elf PRIVATE
    "${SAMPLE1_BSP_ROOT}"
    "${SAMPLE1_COMPONENT_ROOT}/Common"
    "${SAMPLE1_COMPONENT_ROOT}/imx335"
    "${SAMPLE1_COMPONENT_ROOT}/rk050hr18"
    "${SAMPLE1_ISP_ROOT}/isp/Inc"
    "${SAMPLE1_EVISION_ROOT}/Inc")
target_compile_definitions(sample1.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER)
target_link_libraries(sample1.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${SAMPLE1_EVISION_ROOT}/Lib/libn6-evision-st-ae_gcc.a"
    "${SAMPLE1_EVISION_ROOT}/Lib/libn6-evision-awb_gcc.a")

set(SAMPLE1_LINKER_SCRIPT "${UAI_STM32N6570_DK_LINKER_SCRIPT}")
target_link_options(sample1.elf PRIVATE
    "-T${SAMPLE1_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample1.map
    -Wl,--print-memory-usage)
set_target_properties(sample1.elf PROPERTIES
    LINK_DEPENDS "${SAMPLE1_LINKER_SCRIPT}")

add_custom_command(TARGET sample1.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:sample1.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/sample1.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:sample1.elf>
    VERBATIM)

add_custom_target(sample1 DEPENDS sample1.elf)
