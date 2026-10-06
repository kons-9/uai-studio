uai_add_host_test(memory_manager_test
    ../memory_manager.cpp
    memory_manager_test.cpp
)
target_include_directories(memory_manager_test BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${UAI_ROOT}/kernel/middleware"
)