uai_add_host_test(image_resizer_test
    ../image_resizer.cpp
    image_resizer_test.cpp
)
target_include_directories(image_resizer_test PRIVATE "${UAI_ROOT}/kernel/middleware")