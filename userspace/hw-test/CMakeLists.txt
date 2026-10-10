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
find_package(Python3 COMPONENTS Interpreter REQUIRED)
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
    src/tests/integration.cpp
    src/ui/display.cpp
    src/tests/camera/buffers.cpp
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
    $<TARGET_OBJECTS:uai-driver-overrides>
    "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c"
    "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c"
    "${STM32CUBE_N6_DIR}/Utilities/Fonts/font12.c")

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
    "${COMPONENTS}/rk050hr18" "${COMPONENTS}/imx335" "${COMPONENTS}/gt911"
    "${ISP}/isp/Inc" "${ISP}/evision/Inc" "${STM32CUBE_N6_DIR}/Utilities/Fonts")
target_compile_definitions(${TARGET_NAME} PRIVATE
    STM32N6570_DK STM32N6570_DK_DEVELOPMENT_MODE USE_HAL_DRIVER
    STM32N657xx USE_FULL_LL_DRIVER CPU_IN_SECURE_STATE)
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
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" -Wl,--print-memory-usage)
set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS "${_linker_script};${_scratch_script};${_memory_script}")
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/check_link.py"
        --elf "$<TARGET_FILE:${TARGET_NAME}>" --map "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.map" --nm "${CMAKE_NM}"
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET_NAME}>" VERBATIM)
add_custom_target(${APP_TARGET} DEPENDS ${TARGET_NAME})
