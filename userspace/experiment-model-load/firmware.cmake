set(EXPERIMENT_RUNTIME "${CMAKE_CURRENT_LIST_DIR}")
set(_experiment_pre_kernel_default ON)
option(EXPERIMENT_PREKERNEL_READY "Enable integrated camera board configuration" ${_experiment_pre_kernel_default})
if(NOT EXPERIMENT_PREKERNEL_READY)
    message(FATAL_ERROR "pre-kernel integration is disabled for ${APP_TARGET}.")
endif()
if(NOT UAI_CAMERA_BOARD_APP STREQUAL "${APP_TARGET}")
    message(FATAL_ERROR "${APP_TARGET} must use its own camera board profile")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(CAMERA_RUNTIME_SRC "${EXPERIMENT_RUNTIME}/src/camera_runtime")
set(CAMERA_DRIVER_DIR "${CAMERA_RUNTIME_SRC}/driver")
set(CUBE "${STM32CUBE_N6_DIR}")
set(BSP "${CUBE}/Drivers/BSP/STM32N6570-DK")
set(COMPONENTS "${CUBE}/Drivers/BSP/Components")
set(ISP "${CUBE}/Middlewares/ST/STM32_ISP_Library")
set(HAL "${CUBE}/Drivers/STM32N6xx_HAL_Driver")
set(TARGET_NAME ${APP_TARGET}.elf)
if(NOT EXPERIMENT_EXTENSION)
    set(EXPERIMENT_EXTENSION "${EXPERIMENT_RUNTIME}/src/extension.cpp")
endif()
set(CAMERA_BSP "${CAMERA_DRIVER_DIR}/board/stm32n6570_discovery_camera.c")
set_source_files_properties("${CAMERA_BSP}" PROPERTIES COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_FrameEventCallback")

add_executable(${TARGET_NAME}
    "${CAMERA_RUNTIME_SRC}/main.cpp" "${CAMERA_RUNTIME_SRC}/isp_camera.cpp"
    "${CAMERA_RUNTIME_SRC}/bsp_device.cpp" "${CAMERA_RUNTIME_SRC}/frame_events.c"
    ${EXPERIMENT_EXTENSION}
    "${CAMERA_DRIVER_DIR}/camera_driver.cpp"
    "${CAMERA_DRIVER_DIR}/display_driver.cpp"
    "${CAMERA_DRIVER_DIR}/frame_buffer.cpp"
    "${CAMERA_DRIVER_DIR}/board/hal_time.c"
    "${CAMERA_DRIVER_DIR}/board/dcmipp_callbacks.c"
    "${CAMERA_DRIVER_DIR}/board/irq_handlers.c"
    "${CAMERA_BSP}"
    "${BSP}/stm32n6570_discovery_bus.c"
    "${BSP}/stm32n6570_discovery_xspi.c"
    "${BSP}/stm32n6570_discovery_lcd.c"
    "${COMPONENTS}/aps256xx/aps256xx.c"
    "${COMPONENTS}/mx66uw1g45g/mx66uw1g45g.c"
    "${COMPONENTS}/imx335/imx335.c"
    "${COMPONENTS}/imx335/imx335_reg.c"
    "${CAMERA_DRIVER_DIR}/board/isp_core.c"
    "${ISP}/isp/Src/isp_algo.c"
    "${ISP}/isp/Src/isp_services.c")

set_source_files_properties("${CAMERA_DRIVER_DIR}/board/dcmipp_callbacks.c" PROPERTIES COMPILE_DEFINITIONS
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
    "${EXPERIMENT_RUNTIME}/src" "${CAMERA_RUNTIME_SRC}" "${CAMERA_DRIVER_DIR}/board/include"
    "${EXPERIMENT_RUNTIME}/config"
    "${BSP}" "${COMPONENTS}/Common" "${COMPONENTS}/aps256xx"
    "${COMPONENTS}/mx66uw1g45g" "${COMPONENTS}/imx335" "${COMPONENTS}/rk050hr18"
    "${ISP}/isp/Inc" "${ISP}/evision/Inc" "${HAL}/Inc"
    "${CUBE}/Drivers/CMSIS/Device/ST/STM32N6xx/Include" "${CUBE}/Drivers/CMSIS/Include")
target_compile_definitions(${TARGET_NAME} PRIVATE
    PIPE2_PIPE_DUAL=1 PIPE2_IMX335_MIPI891=0 PIPE2_BUFFER_PSRAM=0
    PIPE2_CROP_NATIVE=0 PIPE2_CROP_INTEGER4=0 PIPE2_IMX335_TEST_PATTERN_MODE=-1
    STM32N6570_DK STM32N6570_DK_DEVELOPMENT_MODE USE_HAL_DRIVER STM32N657xx USE_FULL_LL_DRIVER CPU_IN_SECURE_STATE)
if(APP_TARGET STREQUAL "experiment-gpu")
    target_compile_definitions(${TARGET_NAME} PRIVATE EXPERIMENT_GPU_VISUAL=1)
endif()
target_link_libraries(${TARGET_NAME} PRIVATE uai::utkernel uai::stm32n6570_dk
    "${ISP}/evision/Lib/libn6-evision-awb_gcc.a" "${ISP}/evision/Lib/libn6-evision-st-ae_gcc.a" m)
set(LINKER_SCRIPT "${EXPERIMENT_RUNTIME}/camera-runtime-ram.ld")
target_link_options(${TARGET_NAME} PRIVATE "-T${LINKER_SCRIPT}" -Wl,-u,uai_ram_entry
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" -Wl,--print-memory-usage)
set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS "${LINKER_SCRIPT}")
set(_model_link_args)
if(EXPERIMENT_MODEL_NPU)
    list(APPEND _model_link_args --npu)
    if(EXPERIMENT_MODEL_MULTI)
        list(APPEND _model_link_args --multi)
    endif()
endif()
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${EXPERIMENT_RUNTIME}/tool/check_link.py"
        --elf "$<TARGET_FILE:${TARGET_NAME}>" --map "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" --nm "${CMAKE_NM}"
        ${_model_link_args}
    COMMAND ${CMAKE_OBJCOPY} -O binary --remove-section=.model_blob_person --remove-section=.model_blob_face
        --remove-section=.model_blob_seg "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET_NAME}>" VERBATIM)
add_custom_target(${APP_TARGET} DEPENDS ${TARGET_NAME})
