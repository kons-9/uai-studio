cmake_minimum_required(VERSION 3.16)

add_library(uai-middleware STATIC
    ai_runtime/pipeline.cpp
    ai_model_monitor/ai_model_monitor.cpp
    cpu_task_monitor/cpu_task_monitor.cpp
    memory/static_memory_layout.cpp
    image_resizer/image_resizer.cpp
    memory_manager/fixed_pool_allocator.cpp
    memory_manager/memory_manager.cpp)
add_library(uai::middleware ALIAS uai-middleware)

target_include_directories(uai-middleware
    PUBLIC
        "${PROJECT_SOURCE_DIR}/kernel"
        "${PROJECT_SOURCE_DIR}/kernel/middleware"
        "${UAI_GENERATED_INCLUDE_DIR}"
    PRIVATE
        "${CUBEMX_OUTPUT_DIR}/Appli/Core/Inc"
        "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Include")

target_compile_definitions(uai-middleware PRIVATE
    STM32N657xx
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER)
target_compile_options(uai-middleware PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(uai-middleware PUBLIC uai::utkernel)
