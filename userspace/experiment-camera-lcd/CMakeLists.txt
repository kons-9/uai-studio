cmake_minimum_required(VERSION 3.16)

set(CAMERA_LCD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CAMERA_LCD_SRC_DIR "${CAMERA_LCD_ROOT}/src")
set(CAMERA_LCD_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(CAMERA_LCD_BSP_ROOT
    "${CAMERA_LCD_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(CAMERA_LCD_COMPONENT_ROOT
    "${CAMERA_LCD_CUBE_ROOT}/Drivers/BSP/Components")
set(CAMERA_LCD_HAL_ROOT
    "${CAMERA_LCD_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver")
set(CAMERA_LCD_CMSIS_ROOT
    "${CAMERA_LCD_CUBE_ROOT}/Drivers/CMSIS")
set(CAMERA_LCD_ISP_ROOT
    "${CAMERA_LCD_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library")

add_executable(experiment-camera-lcd.elf
    src/main.cpp
    src/driver/camera_driver.cpp
    src/driver/display_driver.cpp
    src/driver/frame_buffer.cpp
    src/driver/board/hal_time.c
    src/driver/board/irq_handlers.c
    "${CAMERA_LCD_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${CAMERA_LCD_BSP_ROOT}/stm32n6570_discovery_camera.c"
    "${CAMERA_LCD_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${CAMERA_LCD_COMPONENT_ROOT}/imx335/imx335.c"
    "${CAMERA_LCD_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${CAMERA_LCD_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${CAMERA_LCD_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${CAMERA_LCD_HAL_ROOT}/Src/stm32n6xx_hal_i2c.c"
    "${CAMERA_LCD_HAL_ROOT}/Src/stm32n6xx_hal_i2c_ex.c"
    "${CAMERA_LCD_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${CAMERA_LCD_ISP_ROOT}/isp/Src/isp_algo.c"
    "${CAMERA_LCD_ISP_ROOT}/isp/Src/isp_core.c"
    "${CAMERA_LCD_ISP_ROOT}/isp/Src/isp_services.c")

target_include_directories(experiment-camera-lcd.elf PRIVATE
    "${CAMERA_LCD_SRC_DIR}"
    "${CAMERA_LCD_SRC_DIR}/driver/board/include"
    "${CAMERA_LCD_ROOT}/config"
    "${CAMERA_LCD_BSP_ROOT}"
    "${CAMERA_LCD_COMPONENT_ROOT}/Common"
    "${CAMERA_LCD_COMPONENT_ROOT}/imx335"
    "${CAMERA_LCD_COMPONENT_ROOT}/rk050hr18"
    "${CAMERA_LCD_ISP_ROOT}/isp/Inc"
    "${CAMERA_LCD_ISP_ROOT}/evision/Inc"
    "${CAMERA_LCD_HAL_ROOT}/Inc"
    "${CAMERA_LCD_CMSIS_ROOT}/Device/ST/STM32N6xx/Include"
    "${CAMERA_LCD_CMSIS_ROOT}/Include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/config"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/mtkernel/include")

target_compile_definitions(experiment-camera-lcd.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)

target_link_libraries(experiment-camera-lcd.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${CAMERA_LCD_ISP_ROOT}/evision/Lib/libn6-evision-awb_gcc.a"
    "${CAMERA_LCD_ISP_ROOT}/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(CAMERA_LCD_LINKER_SCRIPT "${CAMERA_LCD_ROOT}/experiment-camera-lcd-ram.ld")
target_link_options(experiment-camera-lcd.elf PRIVATE
    "-T${CAMERA_LCD_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/experiment-camera-lcd.map
    -Wl,--print-memory-usage)
set_target_properties(experiment-camera-lcd.elf PROPERTIES
    LINK_DEPENDS "${CAMERA_LCD_LINKER_SCRIPT}")

add_custom_command(TARGET experiment-camera-lcd.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:experiment-camera-lcd.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/experiment-camera-lcd.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:experiment-camera-lcd.elf>
    VERBATIM)

add_custom_target(experiment-camera-lcd DEPENDS experiment-camera-lcd.elf)
