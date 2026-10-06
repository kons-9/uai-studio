add_library(image_processing_host_core STATIC ../image_processing.cpp)
target_include_directories(image_processing_host_core PUBLIC
    "${UAI_ROOT}/kernel" "${UAI_ROOT}/kernel/middleware")
target_compile_options(image_processing_host_core PRIVATE -Wall -Wextra -Werror)

uai_add_host_test(image_resizer_test ../image_resizer.cpp image_resizer_test.cpp)
target_link_libraries(image_resizer_test PRIVATE image_processing_host_core)
target_include_directories(image_resizer_test PRIVATE "${UAI_ROOT}/kernel/middleware")