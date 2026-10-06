include(CheckCXXSourceCompiles)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_REQUIRED_INCLUDES
    "${UAI_ROOT}/kernel"
    "${UAI_ROOT}/kernel/utkernel/mtk3_bsp2/mtkernel/include"
)
set(CMAKE_REQUIRED_FLAGS "-Wall -Wextra -Werror")
unset(UAI_LOG_ACCEPTS_TEXT CACHE)
check_cxx_source_compiles("#include \"middleware/foundation/log.hpp\"
int main() {
    char text[] = \"ok\";
    UAI_LOG_INFO(\"value=%s\", text);
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kInfo, text);
}" UAI_LOG_ACCEPTS_TEXT)
if(NOT UAI_LOG_ACCEPTS_TEXT)
    message(FATAL_ERROR "UAI_LOG must accept const char* text")
endif()

unset(UAI_LOG_ACCEPTS_UB CACHE)
check_cxx_source_compiles("#include \"middleware/foundation/log.hpp\"
int main() {
    UB text[] = {0};
    UAI_LOG_INFO(text);
}" UAI_LOG_ACCEPTS_UB)
if(UAI_LOG_ACCEPTS_UB)
    message(FATAL_ERROR "UAI_LOG must reject UB* text")
endif()

uai_add_host_test(stable_aligned_bytes_test stable_aligned_bytes_test.cpp)
uai_add_host_test(fixed_message_slots_test fixed_message_slots_test.cpp)
uai_add_host_test(owned_buffer_test owned_buffer_test.cpp)
uai_add_host_test(error_test error_test.cpp)
target_include_directories(error_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)
uai_add_host_test(message_channel_test message_channel_test.cpp)
target_include_directories(message_channel_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)
uai_add_host_test(task_test task_test.cpp)
target_include_directories(task_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)