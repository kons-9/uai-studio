uai_add_host_test(cpu_task_monitor_test
    ../cpu_task_monitor.cpp
    ../../foundation/error.cpp
    cpu_task_monitor_test.cpp
)
target_compile_definitions(cpu_task_monitor_test PRIVATE
    UAI_CPU_TASK_MONITOR=1
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(cpu_task_monitor_test PRIVATE
        -Wno-cast-function-type
    )
endif()
target_include_directories(cpu_task_monitor_test BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)