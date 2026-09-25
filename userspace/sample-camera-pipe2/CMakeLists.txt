cmake_minimum_required(VERSION 3.16)

set(CAMERA_PIPE2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CAMERA_PIPE2_SRC_DIR "${CAMERA_PIPE2_ROOT}/src")
set(CAMERA_PIPE2_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(CAMERA_PIPE2_BSP_ROOT
    "${CAMERA_PIPE2_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(CAMERA_PIPE2_COMPONENT_ROOT
    "${CAMERA_PIPE2_CUBE_ROOT}/Drivers/BSP/Components")
set(CAMERA_PIPE2_HAL_ROOT
    "${CAMERA_PIPE2_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver")
set(CAMERA_PIPE2_CMSIS_ROOT
    "${CAMERA_PIPE2_CUBE_ROOT}/Drivers/CMSIS")
set(CAMERA_PIPE2_ISP_ROOT
    "${CAMERA_PIPE2_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library")

set(CAMERA_PIPE2_CAMERA_BSP_SOURCE
    "${CAMERA_PIPE2_BSP_ROOT}/stm32n6570_discovery_camera.c")
set_source_files_properties(${CAMERA_PIPE2_CAMERA_BSP_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_FrameEventCallback")

add_executable(sample-camera-pipe2.elf
    src/main.cpp
    src/driver/camera_driver.cpp
    src/driver/display_driver.cpp
    src/driver/frame_buffer.cpp
    src/driver/board/hal_time.c
    src/driver/board/dcmipp_callbacks.c
    src/driver/board/irq_handlers.c
    "${CAMERA_PIPE2_BSP_ROOT}/stm32n6570_discovery_bus.c"
    ${CAMERA_PIPE2_CAMERA_BSP_SOURCE}
    "${CAMERA_PIPE2_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${CAMERA_PIPE2_COMPONENT_ROOT}/imx335/imx335.c"
    "${CAMERA_PIPE2_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${CAMERA_PIPE2_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${CAMERA_PIPE2_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${CAMERA_PIPE2_HAL_ROOT}/Src/stm32n6xx_hal_i2c.c"
    "${CAMERA_PIPE2_HAL_ROOT}/Src/stm32n6xx_hal_i2c_ex.c"
    "${CAMERA_PIPE2_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${CAMERA_PIPE2_ISP_ROOT}/isp/Src/isp_algo.c"
    "${CAMERA_PIPE2_ISP_ROOT}/isp/Src/isp_core.c"
    "${CAMERA_PIPE2_ISP_ROOT}/isp/Src/isp_services.c")

target_include_directories(sample-camera-pipe2.elf PRIVATE
    "${CAMERA_PIPE2_SRC_DIR}"
    "${CAMERA_PIPE2_SRC_DIR}/driver/board/include"
    "${CAMERA_PIPE2_ROOT}/config"
    "${CAMERA_PIPE2_BSP_ROOT}"
    "${CAMERA_PIPE2_COMPONENT_ROOT}/Common"
    "${CAMERA_PIPE2_COMPONENT_ROOT}/imx335"
    "${CAMERA_PIPE2_COMPONENT_ROOT}/rk050hr18"
    "${CAMERA_PIPE2_ISP_ROOT}/isp/Inc"
    "${CAMERA_PIPE2_ISP_ROOT}/evision/Inc"
    "${CAMERA_PIPE2_HAL_ROOT}/Inc"
    "${CAMERA_PIPE2_CMSIS_ROOT}/Device/ST/STM32N6xx/Include"
    "${CAMERA_PIPE2_CMSIS_ROOT}/Include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/config"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/mtkernel/include")

target_compile_definitions(sample-camera-pipe2.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)

target_link_libraries(sample-camera-pipe2.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${CAMERA_PIPE2_ISP_ROOT}/evision/Lib/libn6-evision-awb_gcc.a"
    "${CAMERA_PIPE2_ISP_ROOT}/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(CAMERA_PIPE2_LINKER_SCRIPT "${CAMERA_PIPE2_ROOT}/sample-camera-pipe2-ram.ld")
target_link_options(sample-camera-pipe2.elf PRIVATE
    "-T${CAMERA_PIPE2_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample-camera-pipe2.map
    -Wl,--print-memory-usage)
set_target_properties(sample-camera-pipe2.elf PROPERTIES
    LINK_DEPENDS "${CAMERA_PIPE2_LINKER_SCRIPT}")

add_custom_command(TARGET sample-camera-pipe2.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:sample-camera-pipe2.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/sample-camera-pipe2.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:sample-camera-pipe2.elf>
    VERBATIM)

add_custom_target(sample-camera-pipe2 DEPENDS sample-camera-pipe2.elf)
