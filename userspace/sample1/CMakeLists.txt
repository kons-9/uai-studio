cmake_minimum_required(VERSION 3.16)

set(SAMPLE1_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SAMPLE1_SRC_DIR "${SAMPLE1_ROOT}/src")
set(SAMPLE1_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(SAMPLE1_BSP_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(SAMPLE1_COMPONENT_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/BSP/Components")
set(SAMPLE1_HAL_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver")
set(SAMPLE1_CMSIS_ROOT
    "${SAMPLE1_CUBE_ROOT}/Drivers/CMSIS")
set(SAMPLE1_ISP_ROOT
    "${SAMPLE1_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library")

add_executable(sample1.elf
    src/main.cpp
    src/driver/camera_driver.cpp
    src/driver/display_driver.cpp
    src/driver/frame_buffer.cpp
    src/driver/board/hal_time.c
    src/driver/board/irq_handlers.c
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_camera.c"
    "${SAMPLE1_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${SAMPLE1_COMPONENT_ROOT}/imx335/imx335.c"
    "${SAMPLE1_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_i2c.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_i2c_ex.c"
    "${SAMPLE1_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_algo.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_core.c"
    "${SAMPLE1_ISP_ROOT}/isp/Src/isp_services.c")

target_include_directories(sample1.elf PRIVATE
    "${SAMPLE1_SRC_DIR}"
    "${SAMPLE1_SRC_DIR}/driver/board/include"
    "${SAMPLE1_ROOT}/config"
    "${SAMPLE1_BSP_ROOT}"
    "${SAMPLE1_COMPONENT_ROOT}/Common"
    "${SAMPLE1_COMPONENT_ROOT}/imx335"
    "${SAMPLE1_COMPONENT_ROOT}/rk050hr18"
    "${SAMPLE1_ISP_ROOT}/isp/Inc"
    "${SAMPLE1_ISP_ROOT}/evision/Inc"
    "${SAMPLE1_HAL_ROOT}/Inc"
    "${SAMPLE1_CMSIS_ROOT}/Device/ST/STM32N6xx/Include"
    "${SAMPLE1_CMSIS_ROOT}/Include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/config"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/mtkernel/include")

target_compile_definitions(sample1.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)

target_link_libraries(sample1.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${SAMPLE1_ISP_ROOT}/evision/Lib/libn6-evision-awb_gcc.a"
    "${SAMPLE1_ISP_ROOT}/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(SAMPLE1_LINKER_SCRIPT "${SAMPLE1_ROOT}/sample1-ram.ld")
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
