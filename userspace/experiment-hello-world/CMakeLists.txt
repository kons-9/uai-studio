cmake_minimum_required(VERSION 3.16)

file(GLOB_RECURSE HELLO_WORLD_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(experiment-hello-world.elf ${HELLO_WORLD_SOURCES})

target_link_libraries(experiment-hello-world.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
)

set(HELLO_WORLD_LINKER_SCRIPT
    "${UAI_STM32N6570_DK_LINKER_SCRIPT}")

target_link_options(experiment-hello-world.elf PRIVATE
    "-T${HELLO_WORLD_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/experiment-hello-world.map"
    "-Wl,--print-memory-usage"
)

set_target_properties(experiment-hello-world.elf PROPERTIES
    LINK_DEPENDS "${HELLO_WORLD_LINKER_SCRIPT}"
)

add_custom_command(TARGET experiment-hello-world.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:experiment-hello-world.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/experiment-hello-world.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:experiment-hello-world.elf>"
    VERBATIM
)

add_custom_target(experiment-hello-world DEPENDS experiment-hello-world.elf)
