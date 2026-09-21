cmake_minimum_required(VERSION 3.16)

add_executable(sample0.elf
    src/main.cpp
)

target_link_libraries(sample0.elf PRIVATE
    uai::utkernel
    uai::stm32n6570_dk
)

set(SAMPLE0_LINKER_SCRIPT
    "${UAI_STM32N6570_DK_LINKER_SCRIPT}")

target_link_options(sample0.elf PRIVATE
    "-T${SAMPLE0_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample0.map"
    "-Wl,--print-memory-usage"
)

set_target_properties(sample0.elf PROPERTIES
    LINK_DEPENDS "${SAMPLE0_LINKER_SCRIPT}"
)

add_custom_command(TARGET sample0.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:sample0.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/sample0.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:sample0.elf>"
    VERBATIM
)

add_custom_target(sample0 DEPENDS sample0.elf)
