uai_add_host_test(image_processing_test ../image_processing.cpp image_processing_test.cpp graphics_test.cpp)
target_include_directories(image_processing_test PRIVATE "${UAI_ROOT}/kernel/middleware")
