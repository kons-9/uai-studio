uai_add_host_test(hw_test
    ../src/tests/suite.cpp
    hw_test.cpp
    "${UAI_ROOT}/kernel/middleware/ui/canvas.cpp"
    "${UAI_ROOT}/kernel/middleware/ui/widget.cpp")
target_include_directories(hw_test BEFORE PRIVATE
    ../src
    "${UAI_ROOT}/kernel/middleware")