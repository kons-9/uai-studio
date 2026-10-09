cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "hw-test is an on-device test suite; configure it from the firmware root with APP_TARGET=hw-test")
endif()

set(TARGET_NAME ${APP_TARGET}.elf)
set(HWTEST_AUTORUN_TEST "all" CACHE STRING "Tests to run automatically at startup (default: all)")
if(HWTEST_AUTORUN_TEST STREQUAL "")
    set(HWTEST_AUTORUN_TEST "all" CACHE STRING "Tests to run automatically at startup (default: all)" FORCE)
endif()
set(BSP "${STM32CUBE_N6_DIR}/Drivers/BSP/STM32N6570-DK")
set(COMPONENTS "${STM32CUBE_N6_DIR}/Drivers/BSP/Components")
set(ISP "${STM32CUBE_N6_DIR}/Middlewares/ST/STM32_ISP_Library")
set(CAMERA_RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/src/camera_runtime")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NOT UAI_CAMERA_BOARD_APP STREQUAL APP_TARGET)
    message(FATAL_ERROR "hw-test requires its own integrated camera board profile")
endif()
set(_memory_outputs
    "${UAI_GENERATED_INCLUDE_DIR}/middleware/memory/generated/static_memory_layout/key.hpp"
    "${UAI_GENERATED_INCLUDE_DIR}/middleware/memory/generated/static_memory_layout/raw.hpp"
    "${UAI_GENERATED_INCLUDE_DIR}/middleware/memory/generated/memory_config.hpp"
    "${UAI_GENERATED_INCLUDE_DIR}/memory-contract.ld")
file(GLOB _memory_generator_sources
    "${PROJECT_SOURCE_DIR}/host_app/auto_static_memory_layout/*.py")
add_custom_command(OUTPUT ${_memory_outputs}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/generate_memory.py"
        --input "${CMAKE_CURRENT_SOURCE_DIR}/config/memory_layout.json"
        --output-dir "${UAI_GENERATED_INCLUDE_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_memory.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/memory_layout.json"
        ${_memory_generator_sources}
    VERBATIM)
add_custom_target(hw-test-memory-layout DEPENDS ${_memory_outputs})
foreach(target uai-memory-layout uai-middleware uai-drivers uai-driver-overrides)
    add_dependencies(${target} hw-test-memory-layout)
endforeach()
add_executable(${TARGET_NAME}
    src/main.cpp
    src/integration.cpp
    src/ui/display.cpp
    src/camera_runtime/bsp_device.cpp
    src/camera_runtime/isp_camera.cpp
    src/camera_runtime/frame_events.c
    src/camera_runtime/driver/camera_driver.cpp
    src/camera_runtime/driver/frame_buffer.cpp
    src/camera_runtime/driver/board/stm32n6570_discovery_camera.c
    src/camera_runtime/driver/board/dcmipp_callbacks.c
    src/camera_runtime/driver/board/irq_handlers.c
    src/camera_runtime/driver/board/isp_core.c
    src/tests/suite.cpp
    src/tests/display_driver/test.cpp
    src/tests/rng_driver/test.cpp
    src/tests/hash_driver/test.cpp
    src/tests/crc_driver/test.cpp
    src/tests/gpdma_driver/test.cpp
    src/tests/hpdma_driver/test.cpp
    src/tests/rtc_driver/test.cpp
    src/tests/tim_driver/test.cpp
    src/tests/sram_driver/test.cpp
    src/tests/psram_driver/test.cpp
    src/tests/nor_driver/test.cpp
    src/tests/dma2d_driver/test.cpp
    src/tests/dma2d_driver/suite.cpp
    src/driver/board/hal_time.c
    src/driver/c_bsp/stm32n6570_discovery_xspi.c
    "${BSP}/stm32n6570_discovery_lcd.c"
    "${BSP}/stm32n6570_discovery_bus.c"
    "${COMPONENTS}/imx335/imx335.c"
    "${COMPONENTS}/imx335/imx335_reg.c"
    "${COMPONENTS}/gt911/gt911.c"
    "${COMPONENTS}/gt911/gt911_reg.c"
    "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c"
    "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c"
    "${ISP}/isp/Src/isp_algo.c"
    "${ISP}/isp/Src/isp_services.c"
    "${COMPONENTS}/aps256xx/aps256xx.c"
    "${COMPONENTS}/mx66uw1g45g/mx66uw1g45g.c"
    "${STM32CUBE_N6_DIR}/Utilities/Fonts/font12.c")

set(_generated_hal_config "${CUBEMX_OUTPUT_DIR}/FSBL/Core/Inc/stm32n6xx_hal_conf.h")
foreach(module dcmipp rng hash crc crc_ex rif dma dma_ex rtc rtc_ex tim tim_ex ltdc)
    string(REGEX REPLACE "_ex$" "" _base_module "${module}")
    string(TOUPPER "${_base_module}" _base_module)
    set(_module_enabled OFF)
    if(EXISTS "${_generated_hal_config}")
        file(STRINGS "${_generated_hal_config}" _module_enabled
            REGEX "^#define[ \t]+HAL_${_base_module}_MODULE_ENABLED")
    endif()
    set(_generated_hal_source
        "${CUBEMX_OUTPUT_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_${module}.c")
    if(NOT _module_enabled OR NOT EXISTS "${_generated_hal_source}")
        target_sources(${TARGET_NAME} PRIVATE
            "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_${module}.c")
    endif()
endforeach()

foreach(selection SDMMC ETHERNET USB AUDIO EXTERNAL_INPUT)
    option(HWTEST_ENABLE_${selection} "Select the external ${selection} fixture (compile-time blocked until integrated)" OFF)
endforeach()
if(HWTEST_ENABLE_SDMMC)
    target_sources(${TARGET_NAME} PRIVATE src/tests/sdmmc_driver/test.cpp)
endif()
if(HWTEST_ENABLE_ETHERNET)
    target_sources(${TARGET_NAME} PRIVATE src/tests/ethernet_driver/test.cpp)
endif()
if(HWTEST_ENABLE_USB)
    target_sources(${TARGET_NAME} PRIVATE src/tests/usb_driver/test.cpp)
endif()
if(HWTEST_ENABLE_AUDIO)
    target_sources(${TARGET_NAME} PRIVATE src/tests/audio_driver/test.cpp)
endif()
if(HWTEST_ENABLE_EXTERNAL_INPUT)
    target_sources(${TARGET_NAME} PRIVATE src/tests/external_input_driver/test.cpp)
endif()

target_include_directories(${TARGET_NAME} BEFORE PRIVATE
    config
    "${CAMERA_RUNTIME}/driver/board/include"
    "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc")
target_include_directories(${TARGET_NAME} PRIVATE
    src
    "${CAMERA_RUNTIME}"
    "${BSP}" "${COMPONENTS}/Common"
    "${COMPONENTS}/aps256xx" "${COMPONENTS}/mx66uw1g45g"
    "${COMPONENTS}/rk050hr18" "${COMPONENTS}/imx335" "${COMPONENTS}/gt911"
    "${ISP}/isp/Inc" "${ISP}/evision/Inc" "${STM32CUBE_N6_DIR}/Utilities/Fonts")
target_compile_definitions(${TARGET_NAME} PRIVATE
    STM32N6570_DK STM32N6570_DK_DEVELOPMENT_MODE USE_HAL_DRIVER
    STM32N657xx USE_FULL_LL_DRIVER CPU_IN_SECURE_STATE)
target_compile_definitions(${TARGET_NAME} PRIVATE
    PIPE2_PIPE_DUAL=1 PIPE2_IMX335_MIPI891=0 PIPE2_BUFFER_PSRAM=0
    PIPE2_CROP_NATIVE=0 PIPE2_CROP_INTEGER4=0 PIPE2_IMX335_TEST_PATTERN_MODE=-1)
set_source_files_properties(src/camera_runtime/driver/board/stm32n6570_discovery_camera.c PROPERTIES COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_VsyncEventCallback;HAL_DCMIPP_PIPE_FrameEventCallback=camera_pipe2_bsp_HAL_DCMIPP_PIPE_FrameEventCallback")
set_source_files_properties(src/camera_runtime/driver/board/dcmipp_callbacks.c PROPERTIES COMPILE_DEFINITIONS
    "HAL_DCMIPP_PIPE_VsyncEventCallback=experiment_original_vsync;HAL_DCMIPP_PIPE_FrameEventCallback=experiment_original_frame")
if(NOT HWTEST_AUTORUN_TEST STREQUAL "")
    target_compile_definitions(${TARGET_NAME} PRIVATE HWTEST_AUTORUN_TEST="${HWTEST_AUTORUN_TEST}")
endif()
target_link_libraries(${TARGET_NAME} PRIVATE uai::utkernel uai::stm32n6570_dk uai::middleware uai::drivers
    "${ISP}/evision/Lib/libn6-evision-awb_gcc.a" "${ISP}/evision/Lib/libn6-evision-st-ae_gcc.a" m)

set(_linker_script "${CMAKE_CURRENT_SOURCE_DIR}/camera-runtime-ram.ld")
set(_scratch_script "${CMAKE_CURRENT_SOURCE_DIR}/scratch.ld")
set(_memory_script "${UAI_GENERATED_INCLUDE_DIR}/memory-contract.ld")
add_dependencies(${TARGET_NAME} hw-test-memory-layout)
target_link_options(${TARGET_NAME} PRIVATE
    "-T${_memory_script}" "-T${_scratch_script}" "-T${_linker_script}" -Wl,-u,uai_ram_entry
    # Wrap the reset-phase component calls so the NOR test can report which
    # SPI/OPI reset command times out without patching the Cube BSP sources.
    "-Wl,--wrap=MX66UW1G45G_ReadStatusRegister"
    "-Wl,--wrap=MX66UW1G45G_ResetEnable"
    "-Wl,--wrap=MX66UW1G45G_ResetMemory"
    "-Wl,--wrap=HAL_XSPI_Command"
    "-Wl,--wrap=HAL_XSPI_Receive"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" -Wl,--print-memory-usage)
set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS "${_linker_script};${_scratch_script};${_memory_script}")
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/check_link.py"
        --elf "$<TARGET_FILE:${TARGET_NAME}>" --map "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" --nm "${CMAKE_NM}"
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET_NAME}>" VERBATIM)
add_custom_target(${APP_TARGET} DEPENDS ${TARGET_NAME})
