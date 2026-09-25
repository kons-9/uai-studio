cmake_minimum_required(VERSION 3.16)

set(AI_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(AI_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(AI_CUBE_ROOT "${STM32CUBE_N6_DIR}")
set(AI_BSP_ROOT
    "${AI_CUBE_ROOT}/Drivers/BSP/STM32N6570-DK")
set(AI_COMPONENT_ROOT
    "${AI_CUBE_ROOT}/Drivers/BSP/Components")

# Camera diagnostics are owned by this application. Keep defaults here so
# sample-ai remains configurable even when the kernel driver target is omitted.
if(NOT DEFINED AI_IMX335_TEST_PATTERN_MODE)
    set(AI_IMX335_TEST_PATTERN_MODE "-1" CACHE STRING
        "IMX335 test pattern mode (-1 disables the sensor test pattern)")
endif()
if(NOT DEFINED AI_DCMIPP_BYPASS_DOWNSIZE)
    option(AI_DCMIPP_BYPASS_DOWNSIZE
        "Capture the centered 800x480 sensor window without DCMIPP downsize" OFF)
endif()
if(NOT DEFINED AI_DCMIPP_RAW_DUMP)
    option(AI_DCMIPP_RAW_DUMP
        "Capture one cropped RAW10 frame through DCMIPP Pipe0" OFF)
endif()
if(NOT DEFINED AI_DCMIPP_DISABLE_DEMOSAIC)
    option(AI_DCMIPP_DISABLE_DEMOSAIC
        "Disable Pipe1 Bayer demosaicing for a diagnostic capture" OFF)
endif()
if(NOT DEFINED AI_DCMIPP_DEMOSAIC_LINEAR)
    option(AI_DCMIPP_DEMOSAIC_LINEAR
        "Use linear Pipe1 Bayer demosaicing for a diagnostic capture" OFF)
endif()

add_executable(sample-ai.elf
    src/main.cpp
    src/task/application_initialize_task.cpp
    src/task/camera_render_task.cpp
    src/task/inference_task.cpp
    src/task/task_diagnostics.cpp
    src/driver/camera/registers/imx335_registers.cpp
    src/driver/camera/usecase/camera_use_case.cpp
    src/driver/display_driver.cpp
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
    "${AI_SRC_DIR}/driver/c_bsp/stm32n6570_discovery_xspi.c"
    "${AI_BSP_ROOT}/stm32n6570_discovery_bus.c"
    src/driver/camera/board/stm32n6570_discovery_camera.c
    src/driver/camera/board/imx335.c
    "${AI_CUBE_ROOT}/Drivers/BSP/Components/imx335/imx335_reg.c"
    "${AI_BSP_ROOT}/stm32n6570_discovery_lcd.c"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Src/isp_core.c"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Src/isp_algo.c"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Src/isp_services.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dcmipp.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma2d.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_i2c_ex.c"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_ltdc.c"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g/mx66uw1g45g.c"
    "${AI_COMPONENT_ROOT}/aps256xx/aps256xx.c")

target_include_directories(sample-ai.elf PRIVATE
    "${AI_SRC_DIR}"
    "${AI_SRC_DIR}/driver/board/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person"
    "${AI_THIRDPARTY_DIR}/Inc"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton"
    "${AI_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx"
    "${AI_THIRDPARTY_DIR}/vision_models_pp/Inc"
    "${AI_BSP_ROOT}"
    "${AI_COMPONENT_ROOT}/Common"
    "${AI_COMPONENT_ROOT}/imx335"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/isp/Inc"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Inc"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")

target_compile_definitions(sample-ai.elf PRIVATE
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
target_compile_definitions(sample-ai.elf PRIVATE
    AI_IMX335_TEST_PATTERN_MODE=${AI_IMX335_TEST_PATTERN_MODE}
    AI_DCMIPP_BYPASS_DOWNSIZE=$<BOOL:${AI_DCMIPP_BYPASS_DOWNSIZE}>
    AI_DCMIPP_RAW_DUMP=$<BOOL:${AI_DCMIPP_RAW_DUMP}>
    AI_DCMIPP_DISABLE_DEMOSAIC=$<BOOL:${AI_DCMIPP_DISABLE_DEMOSAIC}>
    AI_DCMIPP_DEMOSAIC_LINEAR=$<BOOL:${AI_DCMIPP_DEMOSAIC_LINEAR}>)
target_compile_options(sample-ai.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(sample-ai.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Lib/libn6-evision-awb_gcc.a"
    "${AI_CUBE_ROOT}/Middlewares/ST/STM32_ISP_Library/evision/Lib/libn6-evision-st-ae_gcc.a"
    m)

set(AI_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/stm32n6570-dk-npu-ram.ld")
target_link_options(sample-ai.elf PRIVATE
    "-T${AI_LINKER_SCRIPT}"
    "-Wl,--undefined=NPU0_IRQHandler"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample-ai.map"
    "-Wl,--print-memory-usage")
set_target_properties(sample-ai.elf PROPERTIES
    LINK_DEPENDS "${AI_LINKER_SCRIPT}")

add_custom_command(TARGET sample-ai.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:sample-ai.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/sample-ai.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:sample-ai.elf>"
    VERBATIM)

add_custom_target(sample-ai DEPENDS sample-ai.elf)

add_library(sample-ai_person STATIC
    src/model_manager/model_person.c
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ecloader.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_cipher.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_dbgtrc.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_lib.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_lib_sw_operators.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_runtime.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_stai_internal.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_aton_util.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_sw_float.c"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton/ll_sw_integer.c"
    "${AI_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx/mcu_cache.c"
    "${AI_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx/npu_cache.c"
    "${AI_THIRDPARTY_DIR}/vision_models_pp/Src/od_pp_st_yolox.c"
    "${AI_THIRDPARTY_DIR}/vision_models_pp/Src/vision_models_pp_maxi_is8.c"
    "${AI_THIRDPARTY_DIR}/vision_models_pp/Src/vision_models_pp.c")

target_include_directories(sample-ai_person PRIVATE
    "${AI_SRC_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/person"
    "${AI_THIRDPARTY_DIR}/Inc"
    "${AI_THIRDPARTY_DIR}/Npu/ll_aton"
    "${AI_THIRDPARTY_DIR}/Npu/Devices/STM32N6xx"
    "${AI_THIRDPARTY_DIR}/vision_models_pp/Inc"
    "${AI_BSP_ROOT}"
    "${AI_COMPONENT_ROOT}/Common"
    "${AI_COMPONENT_ROOT}/mx66uw1g45g"
    "${AI_CUBE_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/Include"
    "${AI_CUBE_ROOT}/Drivers/CMSIS/DSP/Include")
target_compile_definitions(sample-ai_person PRIVATE
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
target_compile_options(sample-ai_person PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;inttypes.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/config/network_blobs_config.h>
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(sample-ai_person PRIVATE
    uai::utkernel
    uai::stm32n6570_dk)
target_link_libraries(sample-ai.elf PRIVATE sample-ai_person)
add_custom_target(sample-ai-person DEPENDS sample-ai_person)
