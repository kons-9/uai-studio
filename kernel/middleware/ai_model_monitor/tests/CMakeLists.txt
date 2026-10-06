uai_add_host_test(ai_model_monitor_test
    ../ai_model_monitor.cpp
    ../../foundation/error.cpp
    ai_model_monitor_test.cpp
)
target_include_directories(ai_model_monitor_test BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)