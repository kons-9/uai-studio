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
    target_include_directories(${name} PRIVATE "${UAI_ROOT}/kernel")
    target_link_libraries(${name} PRIVATE GTest::gtest_main Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_subdirectory("${UAI_ROOT}/kernel/middleware/ai_runtime/tests" ai_runtime)
add_subdirectory("${UAI_ROOT}/kernel/middleware/ai_model_monitor/tests" ai_model_monitor)
add_subdirectory("${UAI_ROOT}/kernel/middleware/image_resizer/tests" image_resizer)
add_subdirectory("${UAI_ROOT}/kernel/middleware/memory_manager/tests" memory_manager)
add_subdirectory("${UAI_ROOT}/kernel/middleware/foundation/tests" foundation)