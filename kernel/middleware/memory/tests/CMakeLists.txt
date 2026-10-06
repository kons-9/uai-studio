uai_add_host_test(static_memory_layout_test static_memory_layout_test.cpp)

# Links the production resolver against a host raw.hpp so Key -> Region
# goes through the same translation unit the firmware uses.
uai_add_host_test(static_memory_layout_resolve_test
    ../static_memory_layout.cpp
    static_memory_layout_resolve_test.cpp
)
target_include_directories(static_memory_layout_resolve_test BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
