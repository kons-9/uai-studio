cmake_minimum_required(VERSION 3.16)
project(uai_middleware_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

get_filename_component(UAI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

option(UAI_TEST_TSAN "Build host tests with ThreadSanitizer" OFF)
if(UAI_TEST_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()

function(uai_add_host_test name)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_include_directories(${name} BEFORE PRIVATE
        "${UAI_ROOT}/kernel/utkernel/linux/include"
        "${UAI_ROOT}/kernel")
    target_link_libraries(${name} PRIVATE GTest::gtest_main Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_subdirectory("${UAI_ROOT}/kernel/middleware/ai_runtime/tests" ai_runtime)
add_subdirectory("${UAI_ROOT}/kernel/middleware/ai_model_monitor/tests" ai_model_monitor)
add_subdirectory("${UAI_ROOT}/kernel/middleware/cpu_task_monitor/tests" cpu_task_monitor)
add_subdirectory("${UAI_ROOT}/kernel/middleware/image_resizer/tests" image_resizer)
add_subdirectory("${UAI_ROOT}/kernel/middleware/image_processing/tests" image_processing)
add_subdirectory("${UAI_ROOT}/kernel/middleware/memory/tests" memory)
add_subdirectory("${UAI_ROOT}/kernel/middleware/memory_manager/tests" memory_manager)
add_subdirectory("${UAI_ROOT}/kernel/middleware/foundation/tests" foundation)
add_subdirectory("${UAI_ROOT}/kernel/middleware/task/tests" task)
add_subdirectory("${UAI_ROOT}/kernel/middleware/buffer/tests" buffer)
add_subdirectory("${UAI_ROOT}/kernel/middleware/message_channel/tests" message_channel)
add_subdirectory("${UAI_ROOT}/kernel/middleware/resource_ownership/tests" resource_ownership)
add_subdirectory("${UAI_ROOT}/kernel/middleware/trace_format/tests" trace_format)
add_subdirectory("${UAI_ROOT}/kernel/middleware/pipeline/tests" pipeline)
add_subdirectory("${UAI_ROOT}/kernel/middleware/ui/tests" ui)
add_subdirectory("${UAI_ROOT}/kernel/driver/dma2d_driver/tests" dma2d_driver)
add_subdirectory("${UAI_ROOT}/kernel/driver/peripheral_driver/tests" peripheral_driver)

uai_add_host_test(frame_channels_test
    "${UAI_ROOT}/kernel/middleware/foundation/error.cpp"
    "${UAI_ROOT}/kernel/middleware/memory_manager/memory_manager.cpp"
    "${UAI_ROOT}/userspace/ai-app/tests/frame_channels_test.cpp"
)
target_include_directories(frame_channels_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/middleware/memory_manager/tests/fixtures"
    "${UAI_ROOT}/kernel/middleware"
    "${UAI_ROOT}/kernel/utkernel/linux/include"
    "${UAI_ROOT}/userspace/ai-app/src"
)

uai_add_host_test(ui_layout_test
    "${UAI_ROOT}/kernel/middleware/foundation/error.cpp"
    "${UAI_ROOT}/kernel/middleware/ui/canvas.cpp"
    "${UAI_ROOT}/kernel/middleware/ui/widget.cpp"
    "${UAI_ROOT}/userspace/ai-app/src/ui/app_ui.cpp"
    "${UAI_ROOT}/userspace/ai-app/tests/ui_layout_test.cpp"
)
target_include_directories(ui_layout_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/middleware"
    "${UAI_ROOT}/kernel/utkernel/linux/include"
    "${UAI_ROOT}/userspace/ai-app/src"
)
