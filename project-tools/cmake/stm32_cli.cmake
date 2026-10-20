# STM32 command-line tool integration.
#
# Make is only the user-facing command wrapper.  Tool discovery, defaults,
# validation, and command construction belong here so the same targets are
# available from CMake directly.

function(uai_add_missing_tool_target target_name description)
    add_custom_target(${target_name}
        COMMAND ${CMAKE_COMMAND} -E echo "${description}"
        COMMAND ${CMAKE_COMMAND} -E false
        USES_TERMINAL
        VERBATIM
    )
endfunction()

set(CUBEMX_EXECUTABLE "$ENV{CUBEMX_EXECUTABLE}" CACHE FILEPATH
    "STM32CubeMX executable (or command on PATH)" FORCE)
if(NOT CUBEMX_EXECUTABLE)
    find_program(_uai_cubemx_executable NAMES STM32CubeMX STM32CubeMX.exe)
    if(_uai_cubemx_executable)
        set(CUBEMX_EXECUTABLE "${_uai_cubemx_executable}" CACHE FILEPATH
            "STM32CubeMX executable (or command on PATH)" FORCE)
    endif()
endif()

if(DEFINED ENV{CUBEMX_IOC} AND NOT "$ENV{CUBEMX_IOC}" STREQUAL "")
    set(CUBEMX_IOC "$ENV{CUBEMX_IOC}" CACHE FILEPATH
        "CubeMX IOC input file" FORCE)
elseif(NOT DEFINED CUBEMX_IOC)
    set(CUBEMX_IOC "" CACHE FILEPATH "CubeMX IOC input file")
endif()
if(NOT CUBEMX_IOC)
    set(CUBEMX_IOC "${UAI_DEFAULT_CUBEMX_IOC}"
        CACHE FILEPATH "CubeMX IOC input file" FORCE)
endif()

if(DEFINED ENV{CUBEMX_OUTPUT_DIR} AND NOT "$ENV{CUBEMX_OUTPUT_DIR}" STREQUAL "")
    set(CUBEMX_OUTPUT_DIR "$ENV{CUBEMX_OUTPUT_DIR}" CACHE PATH
        "Directory for CubeMX-generated sources" FORCE)
elseif(NOT DEFINED CUBEMX_OUTPUT_DIR)
    set(CUBEMX_OUTPUT_DIR "" CACHE PATH
        "Directory for CubeMX-generated sources")
endif()
if(NOT CUBEMX_OUTPUT_DIR)
    set(CUBEMX_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cubemx" CACHE PATH
        "Directory for CubeMX-generated sources" FORCE)
elseif(NOT IS_ABSOLUTE "${CUBEMX_OUTPUT_DIR}")
    get_filename_component(_cubemx_output_dir
        "${CUBEMX_OUTPUT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    set(CUBEMX_OUTPUT_DIR "${_cubemx_output_dir}" CACHE PATH
        "Directory for CubeMX-generated sources" FORCE)
endif()

set(STM32_PROGRAMMER_ROOT "$ENV{STM32_PROGRAMMER_ROOT}" CACHE PATH
    "STM32CubeProgrammer tools directory" FORCE)
set(STM32_PROGRAMMER_CLI "$ENV{STM32_PROGRAMMER_CLI}" CACHE FILEPATH
    "STM32_Programmer_CLI executable" FORCE)
set(STM32_PROGRAMMER_LIB "$ENV{STM32_PROGRAMMER_LIB}" CACHE PATH
    "STM32CubeProgrammer shared-library directory" FORCE)
set(STM32_SIGNING_TOOL_CLI "$ENV{STM32_SIGNING_TOOL_CLI}" CACHE FILEPATH
    "STM32_SigningTool_CLI executable" FORCE)

if(STM32_PROGRAMMER_ROOT)
    set(STM32_PROGRAMMER_CLI
        "${STM32_PROGRAMMER_ROOT}/bin/STM32_Programmer_CLI"
        CACHE FILEPATH "STM32_Programmer_CLI executable" FORCE)
    set(STM32_PROGRAMMER_LIB "${STM32_PROGRAMMER_ROOT}/lib" CACHE PATH
        "STM32CubeProgrammer shared-library directory" FORCE)
    set(STM32_SIGNING_TOOL_CLI
        "${STM32_PROGRAMMER_ROOT}/bin/STM32_SigningTool_CLI"
        CACHE FILEPATH "STM32_SigningTool_CLI executable" FORCE)
endif()

if(NOT STM32_PROGRAMMER_CLI)
    find_program(_uai_programmer_cli
        NAMES STM32_Programmer_CLI STM32_Programmer_CLI.exe)
    if(_uai_programmer_cli)
        set(STM32_PROGRAMMER_CLI "${_uai_programmer_cli}" CACHE FILEPATH
            "STM32_Programmer_CLI executable" FORCE)
    endif()
endif()

if(NOT STM32_SIGNING_TOOL_CLI)
    find_program(_uai_signing_tool_cli
        NAMES STM32_SigningTool_CLI STM32_SigningTool_CLI.exe)
    if(_uai_signing_tool_cli)
        set(STM32_SIGNING_TOOL_CLI "${_uai_signing_tool_cli}" CACHE FILEPATH
            "STM32_SigningTool_CLI executable" FORCE)
    endif()
endif()

set(STM32_SIGN_INPUT "$ENV{STM32_SIGN_INPUT}" CACHE FILEPATH
    "Input binary for STM32_SigningTool_CLI" FORCE)
set(STM32_SIGN_OUTPUT "$ENV{STM32_SIGN_OUTPUT}" CACHE FILEPATH
    "Output binary from STM32_SigningTool_CLI" FORCE)
set(STM32_SIGNING_ARGS "$ENV{STM32_SIGNING_ARGS}" CACHE STRING
    "STM32_SigningTool_CLI arguments, excluding -bin and -o" FORCE)

set(STM32_EXTERNAL_LOADER "$ENV{STM32_EXTERNAL_LOADER}" CACHE FILEPATH
    "STM32N6 external-loader .stldr file" FORCE)
set(STM32_PROGRAM_IMAGE "$ENV{STM32_PROGRAM_IMAGE}" CACHE FILEPATH
    "Image to write to STM32N6 external flash" FORCE)
set(STM32_PROGRAM_ADDRESS "$ENV{STM32_PROGRAM_ADDRESS}" CACHE STRING
    "External flash address passed to STM32_Programmer_CLI" FORCE)
if(NOT STM32_PROGRAM_ADDRESS)
    set(STM32_PROGRAM_ADDRESS "0x70000000" CACHE STRING
        "External flash address passed to STM32_Programmer_CLI" FORCE)
endif()
set(STM32_PROGRAM_PORT "$ENV{STM32_PROGRAM_PORT}" CACHE STRING
    "STM32_Programmer_CLI connection port" FORCE)
if(NOT STM32_PROGRAM_PORT)
    set(STM32_PROGRAM_PORT "swd" CACHE STRING
        "STM32_Programmer_CLI connection port" FORCE)
endif()
set(STM32_PROGRAM_SERIAL "$ENV{STM32_PROGRAM_SERIAL}" CACHE STRING
    "ST-LINK serial number" FORCE)
set(STM32_PROGRAM_EXTRA_ARGS "$ENV{STM32_PROGRAM_EXTRA_ARGS}" CACHE STRING
    "Additional STM32_Programmer_CLI arguments" FORCE)

set(STM32_RAM_IMAGE "$ENV{STM32_RAM_IMAGE}" CACHE FILEPATH
    "RAM image passed to STM32_Programmer_CLI" FORCE)
if(NOT STM32_RAM_IMAGE)
    set(STM32_RAM_IMAGE
        "${CMAKE_BINARY_DIR}/userspace/${APP_TARGET}/${APP_TARGET}.bin"
        CACHE FILEPATH "RAM image passed to STM32_Programmer_CLI" FORCE)
endif()
set(STM32_RAM_ADDRESS "$ENV{STM32_RAM_ADDRESS}" CACHE STRING
    "RAM image load address" FORCE)
if(NOT STM32_RAM_ADDRESS)
    set(STM32_RAM_ADDRESS "0x34000400" CACHE STRING
        "RAM image load address" FORCE)
endif()
set(STM32_RAM_ENTRY "$ENV{STM32_RAM_ENTRY}" CACHE STRING
    "RAM image execution address" FORCE)
if(NOT STM32_RAM_ENTRY)
    set(STM32_RAM_ENTRY "${UAI_DEFAULT_RAM_ENTRY}" CACHE STRING
        "RAM image execution address" FORCE)
endif()
set(STM32_RAM_STACK "$ENV{STM32_RAM_STACK}" CACHE STRING
    "Initial main stack pointer" FORCE)
if(NOT STM32_RAM_STACK)
    set(STM32_RAM_STACK "${UAI_DEFAULT_RAM_STACK}" CACHE STRING
        "Initial main stack pointer" FORCE)
endif()
set(STM32_RAM_XPSR "$ENV{STM32_RAM_XPSR}" CACHE STRING
    "Initial XPSR value" FORCE)
if(NOT STM32_RAM_XPSR)
    set(STM32_RAM_XPSR "0x01000000" CACHE STRING
        "Initial XPSR value" FORCE)
endif()

function(uai_add_stm32_cli_targets app_target)
    if(CUBEMX_EXECUTABLE AND CUBEMX_IOC)
        add_custom_target(cubemx-generate
            COMMAND ${CMAKE_COMMAND} -E env
                    "CUBEMX_EXECUTABLE=${CUBEMX_EXECUTABLE}"
                    "CUBEMX_IOC=${CUBEMX_IOC}"
                    "CUBEMX_OUTPUT_DIR=${CUBEMX_OUTPUT_DIR}"
                    sh "${CMAKE_SOURCE_DIR}/project-tools/scripts/cubemx-generate.sh"
            USES_TERMINAL
            VERBATIM
        )
    else()
        uai_add_missing_tool_target(cubemx-generate
            "cubemx-generate requires CUBEMX_EXECUTABLE and CUBEMX_IOC"
        )
    endif()

    set(_stm32_cli_environment)
    if(STM32_PROGRAMMER_LIB)
        set(_stm32_ld_library_path "${STM32_PROGRAMMER_LIB}")
        if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
            string(APPEND _stm32_ld_library_path ":$ENV{LD_LIBRARY_PATH}")
        endif()
        set(_stm32_cli_environment
            "LD_LIBRARY_PATH=${_stm32_ld_library_path}")
    endif()

    if(STM32_SIGNING_TOOL_CLI AND STM32_SIGN_INPUT AND STM32_SIGN_OUTPUT AND
       STM32_SIGNING_ARGS)
        separate_arguments(_stm32_signing_args NATIVE_COMMAND
            "${STM32_SIGNING_ARGS}")
        add_custom_target(stm32-sign
            COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                    "${STM32_SIGNING_TOOL_CLI}"
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

    set(_programmer_connection "port=${STM32_PROGRAM_PORT}")
    if(STM32_PROGRAM_SERIAL)
        string(APPEND _programmer_connection " sn=${STM32_PROGRAM_SERIAL}")
    endif()

    if(STM32_PROGRAMMER_CLI AND STM32_RAM_IMAGE)
        set(_stm32_ram_entry_symbol "")
        if(TARGET "${app_target}.elf")
            get_property(_stm32_has_ram_entry_symbol
                TARGET "${app_target}.elf"
                PROPERTY UAI_RAM_ENTRY_SYMBOL SET)
            if(_stm32_has_ram_entry_symbol)
                get_target_property(_stm32_ram_entry_symbol
                    "${app_target}.elf" UAI_RAM_ENTRY_SYMBOL)
            endif()
        endif()
        if(_stm32_ram_entry_symbol)
            if(CMAKE_NM)
                get_property(_stm32_ram_connect_under_reset
                    TARGET "${app_target}.elf"
                    PROPERTY UAI_RAM_CONNECT_UNDER_RESET)
                set(_stm32_ram_reset_argument)
                if(_stm32_ram_connect_under_reset)
                    set(_stm32_ram_reset_argument
                        -DSTM32_RAM_CONNECT_UNDER_RESET=ON)
                endif()
                add_custom_target(ram-run
                    COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                            ${CMAKE_COMMAND}
                            "-DSTM32_RAM_NM=${CMAKE_NM}"
                            "-DSTM32_RAM_ELF=$<TARGET_FILE:${app_target}.elf>"
                            "-DSTM32_RAM_ENTRY_SYMBOL=${_stm32_ram_entry_symbol}"
                            "-DSTM32_PROGRAMMER_CLI=${STM32_PROGRAMMER_CLI}"
                            "-DSTM32_PROGRAMMER_CONNECTION=${_programmer_connection}"
                            "-DSTM32_RAM_IMAGE=${STM32_RAM_IMAGE}"
                            "-DSTM32_RAM_ADDRESS=${STM32_RAM_ADDRESS}"
                            "-DSTM32_RAM_STACK=${STM32_RAM_STACK}"
                            "-DSTM32_RAM_XPSR=${STM32_RAM_XPSR}"
                            ${_stm32_ram_reset_argument}
                            -P "${CMAKE_SOURCE_DIR}/project-tools/cmake/stm32_ram_run.cmake"
                    DEPENDS "${app_target}"
                    USES_TERMINAL
                    VERBATIM
                )
            else()
                uai_add_missing_tool_target(ram-run
                    "ram-run needs CMAKE_NM to resolve ${_stm32_ram_entry_symbol} from the ELF"
                )
            endif()
        else()
            add_custom_target(ram-run
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -halt
                        -d "${STM32_RAM_IMAGE}" "${STM32_RAM_ADDRESS}"
                        -v
                        -coreReg "MSP=${STM32_RAM_STACK}"
                                 "PC=${STM32_RAM_ENTRY}"
                                 "XPSR=${STM32_RAM_XPSR}"
                        -run
                DEPENDS "${app_target}"
                USES_TERMINAL
                VERBATIM
            )
        endif()
    else()
        uai_add_missing_tool_target(ram-run
            "ram-run requires STM32_PROGRAMMER_CLI and a built RAM image"
        )
    endif()

    if(STM32_PROGRAMMER_CLI AND STM32_EXTERNAL_LOADER)
        separate_arguments(_stm32_program_extra_args NATIVE_COMMAND
            "${STM32_PROGRAM_EXTRA_ARGS}")
        if(TARGET ${app_target}-flash-images)
            get_target_property(_flash_images ${app_target}-flash-images
            UAI_FLASH_IMAGES)
            get_target_property(_flash_addresses ${app_target}-flash-images
            UAI_FLASH_ADDRESSES)
            list(LENGTH _flash_images _flash_image_count)
            list(LENGTH _flash_addresses _flash_address_count)
            if(NOT _flash_images OR NOT _flash_addresses OR
               NOT _flash_image_count EQUAL _flash_address_count)
                message(FATAL_ERROR
                "${app_target}-flash-images requires matching UAI_FLASH_IMAGES and UAI_FLASH_ADDRESSES")
            endif()
            set(_flash_commands)
            foreach(_flash_image IN LISTS _flash_images)
            list(POP_FRONT _flash_addresses _flash_address)
            set(_flash_destination "${_flash_image}")
            if(NOT _flash_address STREQUAL "AUTO")
                list(APPEND _flash_destination "${_flash_address}")
            endif()
            list(APPEND _flash_commands
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                    "${STM32_PROGRAMMER_CLI}"
                    -c "${_programmer_connection}"
                    -el "${STM32_EXTERNAL_LOADER}"
                    -w ${_flash_destination} -v
                    ${_stm32_program_extra_args})
            endforeach()
            add_custom_target(program
            ${_flash_commands}
            DEPENDS ${app_target}-flash-images ${app_target}
            USES_TERMINAL VERBATIM)
        elseif(STM32_PROGRAM_IMAGE)
            add_custom_target(program
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${STM32_PROGRAM_IMAGE}" "${STM32_PROGRAM_ADDRESS}"
                        -v
                        ${_stm32_program_extra_args}
                USES_TERMINAL
                VERBATIM)
        else()
            uai_add_missing_tool_target(program
                "program requires STM32_PROGRAM_IMAGE for this application")
        endif()
    else()
        uai_add_missing_tool_target(program
            "program requires STM32_PROGRAMMER_CLI and STM32_EXTERNAL_LOADER"
        )
    endif()
endfunction()
