cmake_minimum_required(VERSION 3.16)

set(SAMPLE2_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SAMPLE2_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(SAMPLE2_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(SAMPLE2_BSP_ROOT
    "${SAMPLE2_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(SAMPLE2_COMPONENT_ROOT
    "${SAMPLE2_CUBE_ROOT}/Drivers/BSP/Components")

add_executable(sample2.elf
    src/main.cpp
    src/driver/imager_driver/imager_driver.cpp
    src/driver/cache_controller/cache_controller.cpp
    src/driver/nor_driver/nor_driver.cpp
    src/driver/psram_driver/psram_driver.cpp
    src/driver/rif_controller/rif_controller.cpp
    src/driver/lcd_driver/lcd_driver.cpp
    src/memory_manager/memory_manager.cpp
    src/memory_manager/memory_hardware.cpp
    src/driver/npu_driver/npu_hardware.cpp
    src/driver/npu_driver/npu_driver.cpp
    src/model_manager/model_manager.cpp
    src/model_manager/model_person_adapter.cpp
    # Project-owned ref-compatible BSP with a reset fallback for NOR left in
    # OPI-DTR by the external loader.
    "${SAMPLE2_SRC_DIR}/driver/c_bsp/stm32n6570_discovery_xspi.c"
    "${SAMPLE2_COMPONENT_ROOT}/mx66uw1g45g/mx66uw1g45g.c"
    "${SAMPLE2_COMPONENT_ROOT}/aps256xx/aps256xx.c")

target_include_directories(sample2.elf PRIVATE
    "${SAMPLE2_SRC_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person"
    "${SAMPLE2_THIRDPARTY_DIR}/Inc"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx"
    "${SAMPLE2_THIRDPARTY_DIR}/vision_models_pp/Inc"
    "${SAMPLE2_BSP_ROOT}"
    "${SAMPLE2_COMPONENT_ROOT}/Common"
    "${SAMPLE2_COMPONENT_ROOT}/mx66uw1g45g"
    "${SAMPLE2_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")

target_compile_definitions(sample2.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
    NPU_CACHE_EXTERNAL_HAL_MSP)
target_compile_options(sample2.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(sample2.elf PRIVATE
    uai::driver
    uai::stm32n6570_dk)

set(SAMPLE2_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/stm32n6570-dk-npu-ram.ld")
target_link_options(sample2.elf PRIVATE
    "-T${SAMPLE2_LINKER_SCRIPT}"
    "-Wl,--undefined=NPU0_IRQHandler"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample2.map"
    "-Wl,--print-memory-usage")
set_target_properties(sample2.elf PROPERTIES
    LINK_DEPENDS "${SAMPLE2_LINKER_SCRIPT}")

add_custom_command(TARGET sample2.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:sample2.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/sample2.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:sample2.elf>"
    VERBATIM)

add_custom_target(sample2 DEPENDS sample2.elf)

add_library(sample2_person STATIC
    src/model_manager/model_person.c
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ecloader.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_cipher.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_dbgtrc.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_lib.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_lib_sw_operators.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_runtime.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_stai_internal.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_util.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_sw_float.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton/ll_sw_integer.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx/mcu_cache.c"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx/npu_cache.c"
    "${SAMPLE2_THIRDPARTY_DIR}/vision_models_pp/Src/od_pp_st_yolox.c"
    "${SAMPLE2_THIRDPARTY_DIR}/vision_models_pp/Src/vision_models_pp_maxi_is8.c"
    "${SAMPLE2_THIRDPARTY_DIR}/vision_models_pp/Src/vision_models_pp.c")

target_include_directories(sample2_person PRIVATE
    "${SAMPLE2_SRC_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person"
    "${SAMPLE2_THIRDPARTY_DIR}/Inc"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/ll_aton"
    "${SAMPLE2_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx"
    "${SAMPLE2_THIRDPARTY_DIR}/vision_models_pp/Inc"
    "${SAMPLE2_BSP_ROOT}"
    "${SAMPLE2_COMPONENT_ROOT}/Common"
    "${SAMPLE2_COMPONENT_ROOT}/mx66uw1g45g"
    "${SAMPLE2_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${SAMPLE2_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")
target_compile_definitions(sample2_person PRIVATE
    __int64_t_defined=1
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    CPU_IN_SECURE_STATE
    LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6
    LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL
    LL_ATON_RT_MODE=LL_ATON_RT_ASYNC
    LL_ATON_SW_FALLBACK
    LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1
    NPU_CACHE_EXTERNAL_HAL_MSP)
target_compile_options(sample2_person PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;inttypes.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/config/network_blobs_config.h>
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(sample2_person PRIVATE
    uai::utkernel
    uai::stm32n6570_dk)
target_link_libraries(sample2.elf PRIVATE sample2_person)
add_custom_target(sample2-person DEPENDS sample2_person)
