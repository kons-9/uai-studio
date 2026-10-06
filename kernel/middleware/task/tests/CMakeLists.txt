uai_add_host_test(task_test task_test.cpp ../../foundation/error.cpp)
target_include_directories(task_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)