set(_experiment_pre_kernel_default OFF)
if(APP_TARGET STREQUAL "experiment-camera-control" AND
   UAI_CAMERA_BOARD_APP STREQUAL "experiment-camera-pipe2")
    set(_experiment_pre_kernel_default ON)
endif()
option(EXPERIMENT_PREKERNEL_READY "Enable integrated camera board configuration" ${_experiment_pre_kernel_default})
if(NOT EXPERIMENT_PREKERNEL_READY)
    message(FATAL_ERROR "pre-kernel integration is deferred. See experiment-camera-control/README.md. Host tests can be built with cmake -S userspace/experiment-camera-control.")
endif()
if(NOT UAI_CAMERA_BOARD_APP STREQUAL "experiment-camera-pipe2")
    message(FATAL_ERROR "${APP_TARGET} has no integrated camera-pipe2 board profile")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(EXPERIMENT_RUNTIME "${CMAKE_CURRENT_LIST_DIR}")
set(BASE "${EXPERIMENT_RUNTIME}/../experiment-camera-pipe2")
set(CUBE "${STM32CUBE_N6_DIR}")
set(BSP "${CUBE}/Drivers/BSP/STM32N6570-DK")
set(COMPONENTS "${CUBE}/Drivers/BSP/Components")
set(ISP "${CUBE}/Middlewares/ST/STM32_ISP_Library")
set(HAL "${CUBE}/Drivers/STM32N6xx_HAL_Driver")
set(TARGET_NAME ${APP_TARGET}.elf)
if(NOT EXPERIMENT_EXTENSION)
    set(EXPERIMENT_EXTENSION "${EXPERIMENT_RUNTIME}/src/extension.cpp")
endif()
set(CAMERA_BSP "${BASE}/src/driver/board/stm32n6570_discovery_camera.c")
set_source_files_properties("${CAMERA_BSP}" PROPERTIES COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_FrameEventCallback")

add_executable(${TARGET_NAME}
    "${EXPERIMENT_RUNTIME}/src/main.cpp" "${EXPERIMENT_RUNTIME}/src/isp_camera.cpp"
    "${EXPERIMENT_RUNTIME}/src/bsp_device.cpp" "${EXPERIMENT_RUNTIME}/src/frame_events.c"
    ${EXPERIMENT_EXTENSION}
    "${BASE}/src/driver/camera_driver.cpp"
    "${BASE}/src/driver/display_driver.cpp"
    "${BASE}/src/driver/frame_buffer.cpp"
    "${BASE}/src/driver/board/hal_time.c"
    "${BASE}/src/driver/board/dcmipp_callbacks.c"
    "${BASE}/src/driver/board/irq_handlers.c"
    "${CAMERA_BSP}"
    "${BSP}/stm32n6570_discovery_bus.c"
    "${BSP}/stm32n6570_discovery_xspi.c"
    "${BSP}/stm32n6570_discovery_lcd.c"
    "${COMPONENTS}/aps256xx/aps256xx.c"
    "${COMPONENTS}/mx66uw1g45g/mx66uw1g45g.c"
    "${COMPONENTS}/imx335/imx335.c"
    "${COMPONENTS}/imx335/imx335_reg.c"
    "${BASE}/src/driver/board/isp_core.c"
    "${ISP}/isp/Src/isp_algo.c"
    "${ISP}/isp/Src/isp_services.c")

set_source_files_properties("${BASE}/src/driver/board/dcmipp_callbacks.c" PROPERTIES COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=experiment_original_vsync;HAL_DCMIPP_PIPE_FrameEventCallback=experiment_original_frame")

get_target_property(BOARD_SOURCES stm32n6570_dk SOURCES)
foreach(module dcmipp dma2d ltdc)
    set(existing "${BOARD_SOURCES}")
    list(FILTER existing INCLUDE REGEX "/stm32n6xx_hal_${module}\\.c$")
    if(NOT existing)
        target_sources(${TARGET_NAME} PRIVATE "${HAL}/Src/stm32n6xx_hal_${module}.c")
    endif()
endforeach()
# The pre-kernel object target compiles CubeMX's full HAL source list with the
# FSBL configuration, where I2C is disabled. Compile I2C for this camera app
# with the board HAL configuration instead; the BSP bus driver requires it.
target_sources(${TARGET_NAME} PRIVATE
    "${HAL}/Src/stm32n6xx_hal_i2c.c"
    "${HAL}/Src/stm32n6xx_hal_i2c_ex.c")

target_include_directories(${TARGET_NAME} PRIVATE
    src "${EXPERIMENT_RUNTIME}/src" "${BASE}/src" "${BASE}/src/driver/board/include"
    "${EXPERIMENT_RUNTIME}/../experiment-ai/config"
    "${BSP}" "${COMPONENTS}/Common" "${COMPONENTS}/aps256xx"
    "${COMPONENTS}/mx66uw1g45g" "${COMPONENTS}/imx335" "${COMPONENTS}/rk050hr18"
    "${ISP}/isp/Inc" "${ISP}/evision/Inc" "${HAL}/Inc"
    "${CUBE}/Drivers/CMSIS/Device/ST/STM32N6xx/Include" "${CUBE}/Drivers/CMSIS/Include")
target_compile_definitions(${TARGET_NAME} PRIVATE
    PIPE2_PIPE_DUAL=1 PIPE2_IMX335_MIPI891=0 PIPE2_BUFFER_PSRAM=0
    PIPE2_CROP_NATIVE=0 PIPE2_CROP_INTEGER4=0 PIPE2_IMX335_TEST_PATTERN_MODE=-1
    STM32N6570_DK STM32N6570_DK_DEVELOPMENT_MODE USE_HAL_DRIVER STM32N657xx USE_FULL_LL_DRIVER CPU_IN_SECURE_STATE)
target_link_libraries(${TARGET_NAME} PRIVATE uai::utkernel uai::stm32n6570_dk
    "${ISP}/evision/Lib/libn6-evision-awb_gcc.a" "${ISP}/evision/Lib/libn6-evision-st-ae_gcc.a" m)
set(LINKER_SCRIPT "${BASE}/experiment-camera-pipe2-ram.ld")
target_link_options(${TARGET_NAME} PRIVATE "-T${LINKER_SCRIPT}" -Wl,-u,uai_ram_entry
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" -Wl,--print-memory-usage)
set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS "${LINKER_SCRIPT}")
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${EXPERIMENT_RUNTIME}/check_link.py"
        --elf "$<TARGET_FILE:${TARGET_NAME}>" --map "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" --nm "${CMAKE_NM}"
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET_NAME}>" VERBATIM)
add_custom_target(${APP_TARGET} DEPENDS ${TARGET_NAME})
