cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "experiment-hw-test is an on-device test suite; configure it from the firmware root with APP_TARGET=experiment-hw-test")
endif()

set(TARGET_NAME ${APP_TARGET}.elf)
set(HWTEST_AUTORUN_TEST "all" CACHE STRING "Tests to run automatically at startup (default: all)")
if(HWTEST_AUTORUN_TEST STREQUAL "")
    set(HWTEST_AUTORUN_TEST "all" CACHE STRING "Tests to run automatically at startup (default: all)" FORCE)
endif()
set(BSP "${STM32CUBE_N6_DIR}/Drivers/BSP/STM32N6570-DK")
set(COMPONENTS "${STM32CUBE_N6_DIR}/Drivers/BSP/Components")
add_executable(${TARGET_NAME}
    src/main.cpp
    src/display_log.cpp
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
    src/driver/board/hal_time.c
    src/driver/c_bsp/stm32n6570_discovery_xspi.c
    "${BSP}/stm32n6570_discovery_lcd.c"
    "${COMPONENTS}/aps256xx/aps256xx.c"
    "${COMPONENTS}/mx66uw1g45g/mx66uw1g45g.c"
    "${STM32CUBE_N6_DIR}/Utilities/Fonts/font12.c")

set(_generated_hal_config "${CUBEMX_OUTPUT_DIR}/FSBL/Core/Inc/stm32n6xx_hal_conf.h")
foreach(module dma2d rng hash crc crc_ex rif dma dma_ex rtc rtc_ex tim tim_ex ltdc)
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
    "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc")
target_include_directories(${TARGET_NAME} PRIVATE
    src
    "${BSP}" "${COMPONENTS}/Common"
    "${COMPONENTS}/aps256xx" "${COMPONENTS}/mx66uw1g45g"
    "${COMPONENTS}/rk050hr18" "${STM32CUBE_N6_DIR}/Utilities/Fonts")
target_compile_definitions(${TARGET_NAME} PRIVATE
    STM32N6570_DK STM32N6570_DK_DEVELOPMENT_MODE USE_HAL_DRIVER
    STM32N657xx USE_FULL_LL_DRIVER CPU_IN_SECURE_STATE)
if(NOT HWTEST_AUTORUN_TEST STREQUAL "")
    target_compile_definitions(${TARGET_NAME} PRIVATE HWTEST_AUTORUN_TEST="${HWTEST_AUTORUN_TEST}")
endif()
target_link_libraries(${TARGET_NAME} PRIVATE uai::utkernel uai::stm32n6570_dk)

set(_linker_script "${UAI_STM32N6570_DK_LINKER_SCRIPT}")
set(_scratch_script "${CMAKE_CURRENT_SOURCE_DIR}/scratch.ld")
target_link_options(${TARGET_NAME} PRIVATE
    "-T${_scratch_script}" "-T${_linker_script}" -Wl,-u,uai_ram_entry
    # Wrap the reset-phase component calls so the NOR test can report which
    # SPI/OPI reset command times out without patching the Cube BSP sources.
    "-Wl,--wrap=MX66UW1G45G_ReadStatusRegister"
    "-Wl,--wrap=MX66UW1G45G_ResetEnable"
    "-Wl,--wrap=MX66UW1G45G_ResetMemory"
    "-Wl,--wrap=HAL_XSPI_Command"
    "-Wl,--wrap=HAL_XSPI_Receive"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" -Wl,--print-memory-usage)
set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS "${_linker_script};${_scratch_script}")
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET_NAME}>" VERBATIM)
add_custom_target(${APP_TARGET} DEPENDS ${TARGET_NAME})
