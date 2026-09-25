cmake_minimum_required(VERSION 3.16)

add_executable(sample-hello-world.elf
    src/main.cpp
    src/hal_time.c
)

target_link_libraries(sample-hello-world.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
)

set(HELLO_WORLD_LINKER_SCRIPT
    "${UAI_STM32N6570_DK_LINKER_SCRIPT}")

target_link_options(sample-hello-world.elf PRIVATE
    "-T${HELLO_WORLD_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample-hello-world.map"
    "-Wl,--print-memory-usage"
)

set_target_properties(sample-hello-world.elf PROPERTIES
    LINK_DEPENDS "${HELLO_WORLD_LINKER_SCRIPT}"
)

add_custom_command(TARGET sample-hello-world.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:sample-hello-world.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/sample-hello-world.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:sample-hello-world.elf>"
    VERBATIM
)

add_custom_target(sample-hello-world DEPENDS sample-hello-world.elf)
