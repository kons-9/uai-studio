cmake_minimum_required(VERSION 3.16)

add_library(uai-image-processing STATIC image_processing/image_processing.cpp)
add_library(uai::image_processing ALIAS uai-image-processing)
target_include_directories(uai-image-processing PUBLIC
    "${PROJECT_SOURCE_DIR}/kernel"
    "${PROJECT_SOURCE_DIR}/kernel/middleware")
target_compile_options(uai-image-processing PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)

# The only translation unit that references the generated linker-symbol table
# (raw.hpp). Everything else sees Key -> Region through this target.
add_library(uai-memory-layout STATIC memory/static_memory_layout.cpp)
add_library(uai::memory_layout ALIAS uai-memory-layout)
target_include_directories(uai-memory-layout PUBLIC
    "${PROJECT_SOURCE_DIR}/kernel"
    "${UAI_GENERATED_INCLUDE_DIR}")
target_compile_options(uai-memory-layout PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)

add_library(uai-middleware STATIC
    foundation/error.cpp
    ai_runtime/pipeline.cpp
    ai_model_monitor/ai_model_monitor.cpp
    cpu_task_monitor/cpu_task_monitor.cpp
    image_resizer/image_resizer.cpp
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
target_link_libraries(uai-middleware PUBLIC
    uai::utkernel uai::image_processing uai::memory_layout)
