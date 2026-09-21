cmake_minimum_required(VERSION 3.16)

set(SAMPLE0_LINKER_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/cmake/stm32n6570-dk-ram.ld")

add_executable(sample0.elf
    src/startup.S
    src/board.cpp
    src/main.cpp
)

target_link_libraries(sample0.elf PRIVATE uai::utkernel)

target_compile_definitions(sample0.elf PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
)

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

# The first STM32N6570-DK image is linked into AXI SRAM.  Use a project-local
# pyOCD virtualenv when available, and keep the DFP pack outside the repository
# because it is a separately distributed CMSIS package.  This is deliberately
# named ram-run: it is a debugger load, not a persistent Flash programming
# operation.
set(PYOCD_PYTHON "${CMAKE_SOURCE_DIR}/.venv/bin/python"
    CACHE FILEPATH "Python interpreter used for the pyOCD flash target")
if(NOT EXISTS "${PYOCD_PYTHON}")
    find_program(PYOCD_PYTHON NAMES python3 python)
endif()

set(PYOCD_PACK "$ENV{PYOCD_PACK}" CACHE FILEPATH
    "Path to Keil.STM32N6xx_DFP.1.2.0.pack")
set(PYOCD_TARGET "stm32n657x0hxq" CACHE STRING "pyOCD target type")
set(PYOCD_PROBE "" CACHE STRING "Optional pyOCD probe UID")
set(PYOCD_FREQUENCY "2M" CACHE STRING "pyOCD SWD frequency")

if(PYOCD_PACK AND NOT EXISTS "${PYOCD_PACK}")
    message(FATAL_ERROR "PYOCD_PACK does not exist: ${PYOCD_PACK}")
endif()

set(_pyocd_flash_args
    "${CMAKE_SOURCE_DIR}/tools/pyocd_ram_load.py"
    --pack "${PYOCD_PACK}"
    --target "${PYOCD_TARGET}"
    --frequency "${PYOCD_FREQUENCY}"
)
if(PYOCD_PROBE)
    list(APPEND _pyocd_flash_args --probe "${PYOCD_PROBE}")
endif()

if(PYOCD_PYTHON AND PYOCD_PACK)
    add_custom_target(ram-run
        COMMAND "${PYOCD_PYTHON}" ${_pyocd_flash_args}
                "$<TARGET_FILE:sample0.elf>"
        DEPENDS sample0.elf
        USES_TERMINAL
        VERBATIM
    )
else()
    add_custom_target(ram-run
        COMMAND ${CMAKE_COMMAND} -E echo
                "ram-run requires PYOCD_PACK and a Python environment with pyocd"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
endif()
