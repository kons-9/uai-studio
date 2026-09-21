cmake_minimum_required(VERSION 3.16)

add_executable(sample1.elf
    src/main.cpp)

# Keep the application responsible only for sequencing and error reporting.
# BSP, HAL, ISP, IRQ, media SRAM, and µT-Kernel HAL glue live in kernel/driver.
target_link_libraries(sample1.elf PRIVATE
    uai::driver
    uai::stm32n6570_dk)

set(SAMPLE1_LINKER_SCRIPT "${UAI_STM32N6570_DK_LINKER_SCRIPT}")
target_link_options(sample1.elf PRIVATE
    "-T${SAMPLE1_LINKER_SCRIPT}"
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sample1.map
    -Wl,--print-memory-usage)
set_target_properties(sample1.elf PROPERTIES
    LINK_DEPENDS "${SAMPLE1_LINKER_SCRIPT}")

add_custom_command(TARGET sample1.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:sample1.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/sample1.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:sample1.elf>
    VERBATIM)

add_custom_target(sample1 DEPENDS sample1.elf)
