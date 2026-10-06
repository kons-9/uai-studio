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
uai_add_host_test(error_test error_test.cpp ../error.cpp)
target_compile_definitions(error_test PRIVATE
    UAI_EXPECT_LOG_LEVEL=kInfo UAI_EXPECT_ROUTINE_LOG=0
)
target_include_directories(error_test BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

set(error_log_header "${CMAKE_CURRENT_SOURCE_DIR}/../log.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${error_log_header}")
file(READ "${error_log_header}" error_log_source)
set(error_log_default "inline constexpr LogLevel kLogLevel = LogLevel::kInfo;")
string(FIND "${error_log_source}" "${error_log_default}" error_log_default_offset)
if(error_log_default_offset EQUAL -1)
    message(FATAL_ERROR "Update error log tests for the changed default log level")
endif()
foreach(level IN ITEMS Error Debug)
    string(TOLOWER "${level}" level_name)
    set(log_test "error_log_${level_name}_test")
    set(log_include "${CMAKE_CURRENT_BINARY_DIR}/log_${level_name}")
    string(REPLACE "${error_log_default}"
        "inline constexpr LogLevel kLogLevel = LogLevel::k${level};"
        error_log_variant "${error_log_source}")
    file(GENERATE OUTPUT "${log_include}/middleware/foundation/log.hpp"
        CONTENT "${error_log_variant}")
    uai_add_host_test(${log_test} error_test.cpp ../error.cpp)
    target_include_directories(${log_test} BEFORE PRIVATE
        "${log_include}"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_compile_definitions(${log_test} PRIVATE
        UAI_EXPECT_LOG_LEVEL=k${level}
        UAI_EXPECT_ROUTINE_LOG=$<STREQUAL:${level},Debug>
    )
endforeach()

uai_add_host_test(message_channel_test message_channel_test.cpp)
target_include_directories(message_channel_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)
uai_add_host_test(task_test task_test.cpp ../error.cpp)
target_include_directories(task_test BEFORE PRIVATE
    "${UAI_ROOT}/kernel/utkernel/linux/include"
)