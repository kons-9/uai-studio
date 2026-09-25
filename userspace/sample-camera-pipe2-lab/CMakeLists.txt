cmake_minimum_required(VERSION 3.16)

set(CAMERA_PIPE2_LAB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CAMERA_PIPE2_LAB_SRC_DIR "${CAMERA_PIPE2_LAB_ROOT}/src")
set(CAMERA_PIPE2_LAB_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(CAMERA_PIPE2_LAB_BSP_ROOT
    "${CAMERA_PIPE2_LAB_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(CAMERA_PIPE2_LAB_COMPONENT_ROOT
    "${CAMERA_PIPE2_LAB_CUBE_ROOT}/Drivers/BSP/Components")
set(CAMERA_PIPE2_LAB_HAL_ROOT
    "${CAMERA_PIPE2_LAB_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver")
set(CAMERA_PIPE2_LAB_CMSIS_ROOT
    "${CAMERA_PIPE2_LAB_CUBE_ROOT}/Drivers/CMSIS")
set(CAMERA_PIPE2_LAB_ISP_ROOT
    "${CAMERA_PIPE2_LAB_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library")

set(PIPE2_LAB_PIPE_MODE "dual" CACHE STRING
    "Camera pipe layout: dual (400+400) or single (800)")
set_property(CACHE PIPE2_LAB_PIPE_MODE PROPERTY STRINGS dual single)
set(PIPE2_LAB_IMX335_PROFILE "stock" CACHE STRING
    "Sensor/CSI profile: stock (BT1600) or mipi891 (BT900)")
set_property(CACHE PIPE2_LAB_IMX335_PROFILE PROPERTY STRINGS stock mipi891)
set(PIPE2_LAB_BUFFER_MODE "sram" CACHE STRING
    "Capture buffers: sram or psram fixed addresses")
set_property(CACHE PIPE2_LAB_BUFFER_MODE PROPERTY STRINGS sram psram)
set(PIPE2_LAB_CROP_MODE "downsize" CACHE STRING
    "Sensor crop path: downsize, integer4, or native 800x480 crop")
set_property(CACHE PIPE2_LAB_CROP_MODE PROPERTY STRINGS downsize integer4 native)
set(PIPE2_LAB_IMX335_TEST_PATTERN_MODE "-1" CACHE STRING
    "IMX335 test pattern: -1 disables it; 0..11 select a sensor pattern")

if(NOT PIPE2_LAB_PIPE_MODE MATCHES "^(dual|single)$")
    message(FATAL_ERROR "PIPE2_LAB_PIPE_MODE must be dual or single")
endif()
if(NOT PIPE2_LAB_IMX335_PROFILE MATCHES "^(stock|mipi891)$")
    message(FATAL_ERROR "PIPE2_LAB_IMX335_PROFILE must be stock or mipi891")
endif()
if(NOT PIPE2_LAB_BUFFER_MODE MATCHES "^(sram|psram)$")
    message(FATAL_ERROR "PIPE2_LAB_BUFFER_MODE must be sram or psram")
endif()
if(NOT PIPE2_LAB_CROP_MODE MATCHES "^(downsize|integer4|native)$")
    message(FATAL_ERROR
        "PIPE2_LAB_CROP_MODE must be downsize, integer4, or native")
endif()
if(PIPE2_LAB_CROP_MODE STREQUAL "native" AND
   NOT PIPE2_LAB_PIPE_MODE STREQUAL "single")
    message(FATAL_ERROR
        "PIPE2_LAB_CROP_MODE=native requires PIPE2_LAB_PIPE_MODE=single")
endif()
if(PIPE2_LAB_CROP_MODE STREQUAL "integer4" AND
   NOT PIPE2_LAB_PIPE_MODE STREQUAL "dual")
    message(FATAL_ERROR
        "PIPE2_LAB_CROP_MODE=integer4 requires PIPE2_LAB_PIPE_MODE=dual")
endif()
if(NOT PIPE2_LAB_IMX335_TEST_PATTERN_MODE MATCHES "^-?[0-9]+$" OR
   PIPE2_LAB_IMX335_TEST_PATTERN_MODE LESS -1 OR
   PIPE2_LAB_IMX335_TEST_PATTERN_MODE GREATER 11)
    message(FATAL_ERROR
        "PIPE2_LAB_IMX335_TEST_PATTERN_MODE must be between -1 and 11")
endif()

set(PIPE2_LAB_PROFILE_DEFINES)
list(APPEND PIPE2_LAB_PROFILE_DEFINES
    PIPE2_LAB_IMX335_TEST_PATTERN_MODE=${PIPE2_LAB_IMX335_TEST_PATTERN_MODE})
if(PIPE2_LAB_CROP_MODE STREQUAL "native")
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_CROP_NATIVE=1)
else()
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_CROP_NATIVE=0)
endif()
if(PIPE2_LAB_CROP_MODE STREQUAL "integer4")
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_CROP_INTEGER4=1)
else()
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_CROP_INTEGER4=0)
endif()
if(PIPE2_LAB_PIPE_MODE STREQUAL "dual")
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_PIPE_DUAL=1)
else()
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_PIPE_DUAL=0)
endif()
if(PIPE2_LAB_IMX335_PROFILE STREQUAL "mipi891")
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_IMX335_MIPI891=1)
else()
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_IMX335_MIPI891=0)
endif()
if(PIPE2_LAB_BUFFER_MODE STREQUAL "psram")
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_BUFFER_PSRAM=1)
else()
    list(APPEND PIPE2_LAB_PROFILE_DEFINES PIPE2_LAB_BUFFER_PSRAM=0)
endif()

set(CAMERA_PIPE2_LAB_CAMERA_BSP_SOURCE
    "${CAMERA_PIPE2_LAB_SRC_DIR}/driver/board/stm32n6570_discovery_camera.c")
set_source_files_properties(${CAMERA_PIPE2_LAB_CAMERA_BSP_SOURCE} PROPERTIES
    COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_FrameEventCallback")

add_executable(sample-camera-pipe2-lab.elf
    src/main.cpp
    src/driver/camera_driver.cpp
    src/driver/display_driver.cpp
    src/driver/frame_buffer.cpp
    src/driver/board/hal_time.c
    src/driver/board/dcmipp_callbacks.c
    src/driver/board/irq_handlers.c
    "${CAMERA_PIPE2_LAB_BSP_ROOT}/stm32n6570_discovery_bus.c"
    "${CAMERA_PIPE2_LAB_BSP_ROOT}/stm32n6570_discovery_xspi.c"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/aps256xx/aps256xx.c"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/mx66uw1g45g/mx66uw1g45g.c"
    ${CAMERA_PIPE2_LAB_CAMERA_BSP_SOURCE}
    "${CAMERA_PIPE2_LAB_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/imx335/imx335.c"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/imx335/imx335_reg.c"
    "${CAMERA_PIPE2_LAB_HAL_ROOT}/Src/stm32n6xx_hal_dcmipp.c"
    "${CAMERA_PIPE2_LAB_HAL_ROOT}/Src/stm32n6xx_hal_dma2d.c"
    "${CAMERA_PIPE2_LAB_HAL_ROOT}/Src/stm32n6xx_hal_i2c.c"
    "${CAMERA_PIPE2_LAB_HAL_ROOT}/Src/stm32n6xx_hal_i2c_ex.c"
    "${CAMERA_PIPE2_LAB_HAL_ROOT}/Src/stm32n6xx_hal_ltdc.c"
    "${CAMERA_PIPE2_LAB_ISP_ROOT}/isp/Src/isp_algo.c"
    "${CAMERA_PIPE2_LAB_SRC_DIR}/driver/board/isp_core.c"
    "${CAMERA_PIPE2_LAB_ISP_ROOT}/isp/Src/isp_services.c")

target_include_directories(sample-camera-pipe2-lab.elf PRIVATE
    "${CAMERA_PIPE2_LAB_SRC_DIR}"
    "${CAMERA_PIPE2_LAB_SRC_DIR}/driver/board/include"
    "${CAMERA_PIPE2_LAB_ROOT}/config"
    "${CAMERA_PIPE2_LAB_BSP_ROOT}"
    "${CMAKE_SOURCE_DIR}/userspace/sample-ai/config"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/Common"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/aps256xx"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/mx66uw1g45g"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/imx335"
    "${CAMERA_PIPE2_LAB_COMPONENT_ROOT}/rk050hr18"
    "${CAMERA_PIPE2_LAB_ISP_ROOT}/isp/Inc"
    "${CAMERA_PIPE2_LAB_ISP_ROOT}/evision/Inc"
    "${CAMERA_PIPE2_LAB_HAL_ROOT}/Inc"
    "${CAMERA_PIPE2_LAB_CMSIS_ROOT}/Device/ST/STM32N6xx/Include"
    "${CAMERA_PIPE2_LAB_CMSIS_ROOT}/Include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/config"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/include"
    "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/mtkernel/include")

target_compile_definitions(sample-camera-pipe2-lab.elf PRIVATE
    ${PIPE2_LAB_PROFILE_DEFINES}
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE)

target_link_libraries(sample-camera-pipe2-lab.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${CAMERA_PIPE2_LAB_ISP_ROOT}/evision/Lib/libn6-evision-awb_gcc.a"
    "${CAMERA_PIPE2_LAB_ISP_ROOT}/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(CAMERA_PIPE2_LAB_LINKER_SCRIPT "${CAMERA_PIPE2_LAB_ROOT}/sample-camera-pipe2-lab-ram.ld")
target_link_options(sample-camera-pipe2-lab.elf PRIVATE
    "-T${CAMERA_PIPE2_LAB_LINKER_SCRIPT}"
    -Wl,-u,uai_ram_entry
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample-camera-pipe2-lab.map
    -Wl,--print-memory-usage)
set_target_properties(sample-camera-pipe2-lab.elf PROPERTIES
    LINK_DEPENDS "${CAMERA_PIPE2_LAB_LINKER_SCRIPT}")

add_custom_command(TARGET sample-camera-pipe2-lab.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:sample-camera-pipe2-lab.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/sample-camera-pipe2-lab.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:sample-camera-pipe2-lab.elf>
    VERBATIM)

add_custom_target(sample-camera-pipe2-lab DEPENDS sample-camera-pipe2-lab.elf)
