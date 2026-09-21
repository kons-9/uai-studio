# Optional STM32CubeMX / STM32CubeProgrammer command-line integration.
#
# These targets intentionally do not run during configure or a normal build.
# The first sample is still a RAM-development image; programming external
# flash requires an FSBL/application image and the matching external loader.

function(uai_add_missing_tool_target target_name description)
    add_custom_target(${target_name}
        COMMAND ${CMAKE_COMMAND} -E echo "${description}"
        COMMAND ${CMAKE_COMMAND} -E false
        USES_TERMINAL
        VERBATIM
    )
endfunction()

set(CUBEMX_EXECUTABLE "$ENV{CUBEMX_EXECUTABLE}" CACHE FILEPATH
    "STM32CubeMX executable (or command on PATH)")
set(CUBEMX_SCRIPT "$ENV{CUBEMX_SCRIPT}" CACHE FILEPATH
    "CubeMX -q script to execute")
set(CUBEMX_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" CACHE PATH
    "Working directory for the CubeMX CLI")

if(NOT CUBEMX_EXECUTABLE)
    find_program(_uai_cubemx_executable NAMES STM32CubeMX STM32CubeMX.exe)
    if(_uai_cubemx_executable)
        set(CUBEMX_EXECUTABLE "${_uai_cubemx_executable}" CACHE FILEPATH
            "STM32CubeMX executable (or command on PATH)" FORCE)
    endif()
endif()

if(CUBEMX_EXECUTABLE AND CUBEMX_SCRIPT)
    add_custom_target(cubemx-generate
        COMMAND "${CUBEMX_EXECUTABLE}" -q "${CUBEMX_SCRIPT}"
        WORKING_DIRECTORY "${CUBEMX_WORKING_DIRECTORY}"
        USES_TERMINAL
        VERBATIM
    )
else()
    uai_add_missing_tool_target(cubemx-generate
        "cubemx-generate requires CUBEMX_EXECUTABLE and CUBEMX_SCRIPT"
    )
endif()

set(STM32_SIGNING_TOOL_CLI "$ENV{STM32_SIGNING_TOOL_CLI}" CACHE FILEPATH
    "STM32_SigningTool_CLI executable")
set(STM32_SIGN_INPUT "$ENV{STM32_SIGN_INPUT}" CACHE FILEPATH
    "Input binary for STM32_SigningTool_CLI")
set(STM32_SIGN_OUTPUT "$ENV{STM32_SIGN_OUTPUT}" CACHE FILEPATH
    "Output binary from STM32_SigningTool_CLI")
set(STM32_SIGNING_ARGS "$ENV{STM32_SIGNING_ARGS}" CACHE STRING
    "STM32_SigningTool_CLI arguments, excluding -bin and -o")

if(NOT STM32_SIGNING_TOOL_CLI)
    find_program(_uai_signing_tool_cli
        NAMES STM32_SigningTool_CLI STM32_SigningTool_CLI.exe)
    if(_uai_signing_tool_cli)
        set(STM32_SIGNING_TOOL_CLI "${_uai_signing_tool_cli}" CACHE FILEPATH
            "STM32_SigningTool_CLI executable" FORCE)
    endif()
endif()

if(STM32_SIGNING_TOOL_CLI AND STM32_SIGN_INPUT AND STM32_SIGN_OUTPUT AND
   STM32_SIGNING_ARGS)
    separate_arguments(_stm32_signing_args NATIVE_COMMAND
        "${STM32_SIGNING_ARGS}")
    add_custom_target(stm32-sign
        COMMAND "${STM32_SIGNING_TOOL_CLI}"
                -bin "${STM32_SIGN_INPUT}"
                ${_stm32_signing_args}
                -o "${STM32_SIGN_OUTPUT}"
        USES_TERMINAL
        VERBATIM
    )
else()
    uai_add_missing_tool_target(stm32-sign
        "stm32-sign requires STM32_SIGNING_TOOL_CLI, STM32_SIGN_INPUT, STM32_SIGN_OUTPUT, and STM32_SIGNING_ARGS"
    )
endif()

set(STM32_PROGRAMMER_CLI "$ENV{STM32_PROGRAMMER_CLI}" CACHE FILEPATH
    "STM32_Programmer_CLI executable")
set(STM32_EXTERNAL_LOADER "$ENV{STM32_EXTERNAL_LOADER}" CACHE FILEPATH
    "STM32N6 external-loader .stldr file")
set(STM32_PROGRAM_IMAGE "$ENV{STM32_PROGRAM_IMAGE}" CACHE FILEPATH
    "Image to write to STM32N6 external flash")
set(STM32_PROGRAM_ADDRESS "0x70000000" CACHE STRING
    "External flash address passed to STM32_Programmer_CLI")
set(STM32_PROGRAM_PORT "swd" CACHE STRING
    "STM32_Programmer_CLI connection port")
set(STM32_PROGRAM_EXTRA_ARGS "$ENV{STM32_PROGRAM_EXTRA_ARGS}" CACHE STRING
    "Additional STM32_Programmer_CLI arguments")

if(NOT STM32_PROGRAMMER_CLI)
    find_program(_uai_programmer_cli
        NAMES STM32_Programmer_CLI STM32_Programmer_CLI.exe)
    if(_uai_programmer_cli)
        set(STM32_PROGRAMMER_CLI "${_uai_programmer_cli}" CACHE FILEPATH
            "STM32_Programmer_CLI executable" FORCE)
    endif()
endif()

if(STM32_PROGRAMMER_CLI AND STM32_EXTERNAL_LOADER AND STM32_PROGRAM_IMAGE)
    separate_arguments(_stm32_program_extra_args NATIVE_COMMAND
        "${STM32_PROGRAM_EXTRA_ARGS}")
    add_custom_target(program
        COMMAND "${STM32_PROGRAMMER_CLI}"
                -c "port=${STM32_PROGRAM_PORT}"
                -el "${STM32_EXTERNAL_LOADER}"
                -w "${STM32_PROGRAM_IMAGE}" "${STM32_PROGRAM_ADDRESS}"
                -v
                ${_stm32_program_extra_args}
        USES_TERMINAL
        VERBATIM
    )
else()
    uai_add_missing_tool_target(program
        "program requires STM32_PROGRAMMER_CLI, STM32_EXTERNAL_LOADER, and STM32_PROGRAM_IMAGE"
    )
endif()
