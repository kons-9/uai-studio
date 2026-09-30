cmake_minimum_required(VERSION 3.16)

if(NOT APP_TARGET STREQUAL "ai-app")
    message(FATAL_ERROR "kernel/middleware is currently used by ai-app only")
endif()

set(AI_APP_SOURCE_DIR "${PROJECT_SOURCE_DIR}/userspace/ai-app/src")
set(AI_APP_CONFIG_DIR "${PROJECT_SOURCE_DIR}/userspace/ai-app/config")
set(AI_MEMORY_LAYOUT_GENERATED_DIR "${PROJECT_SOURCE_DIR}/userspace/ai-app/generated")

add_library(ai-app-middleware STATIC
    ai_runtime/pipeline.cpp
    ai_model_monitor/ai_model_monitor.cpp
    cpu_task_monitor/cpu_task_monitor.cpp
    memory/static_memory_layout.cpp
    image_resizer/image_resizer.cpp
    memory_manager/fixed_pool_allocator.cpp
    memory_manager/memory_manager.cpp)
add_library(uai::ai_app_middleware ALIAS ai-app-middleware)

target_include_directories(ai-app-middleware
    PUBLIC
        "${PROJECT_SOURCE_DIR}/kernel"
        "${PROJECT_SOURCE_DIR}/kernel/middleware"
        "${AI_APP_SOURCE_DIR}"
        "${AI_MEMORY_LAYOUT_GENERATED_DIR}"
    PRIVATE
        "${AI_APP_CONFIG_DIR}"
        "${CUBEMX_OUTPUT_DIR}/Appli/Core/Inc"
        "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Include")

target_compile_definitions(ai-app-middleware PRIVATE
    STM32N657xx
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER)
target_compile_options(ai-app-middleware PRIVATE
    -O3
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
target_link_libraries(ai-app-middleware PUBLIC uai::utkernel)
