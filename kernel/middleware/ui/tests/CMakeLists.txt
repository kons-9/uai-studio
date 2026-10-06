uai_add_host_test(ui_test ../canvas.cpp ../widget.cpp ui_test.cpp)
target_include_directories(ui_test PRIVATE "${UAI_ROOT}/kernel/middleware")
