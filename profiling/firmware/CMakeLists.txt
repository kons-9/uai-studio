cmake_minimum_required(VERSION 3.16)
project(uai_studio_firmware CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform selection
option(UAI_PLATFORM_SIM "Build for simulation (PC)" ON)
option(UAI_TRACE_MODE "Enable tracing (STATIC or DYNAMIC)" "DYNAMIC")

# Tracing firmware lives in a sibling directory
set(TRACING_FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tracing/firmware")

# Sources (library — tracing infrastructure)
set(FIRMWARE_SOURCES
    ${TRACING_FW_DIR}/trace_hook.cpp
)

# Library
add_library(uai_studio_fw STATIC ${FIRMWARE_SOURCES})

target_include_directories(uai_studio_fw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TRACING_FW_DIR}
)

# Platform defines
if(UAI_PLATFORM_SIM)
    target_compile_definitions(uai_studio_fw PUBLIC UAI_PLATFORM_SIM)
endif()

# Trace mode
if(UAI_TRACE_MODE STREQUAL "STATIC")
    target_compile_definitions(uai_studio_fw PUBLIC UAI_TRACE_STATIC)
elseif(UAI_TRACE_MODE STREQUAL "DYNAMIC")
    target_compile_definitions(uai_studio_fw PUBLIC UAI_TRACE_DYNAMIC)
endif()

# Profile mode executable (simulation)
if(UAI_PLATFORM_SIM)
    add_executable(uai_studio_profile main.cpp)
    target_link_libraries(uai_studio_profile PRIVATE uai_studio_fw)

    if(WIN32)
        target_link_libraries(uai_studio_profile PRIVATE ws2_32)
    endif()
endif()
