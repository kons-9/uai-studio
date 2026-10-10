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
    set(_uai_default_ioc
        "${CMAKE_SOURCE_DIR}/userspace/${APP_TARGET}/config/stm32n6570-dk-fullsecure.ioc")
    if(APP_TARGET IN_LIST UAI_KERNEL_APPS)
        set(_uai_default_ioc
            "${CMAKE_SOURCE_DIR}/userspace/${APP_TARGET}/config/stm32n6570-dk-${APP_TARGET}.ioc")
    endif()
    set(CUBEMX_IOC "${_uai_default_ioc}"
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
    if(APP_TARGET STREQUAL "ai-app")
        set(_stm32_default_ram_entry "0x34062001")
    elseif(APP_TARGET IN_LIST UAI_KERNEL_APPS)
        set(_stm32_default_ram_entry "0x34060001")
    elseif(APP_TARGET STREQUAL "experiment-ai")
        set(_stm32_default_ram_entry "0x34052001")
    else()
        set(_stm32_default_ram_entry "0x34000800")
    endif()
    set(STM32_RAM_ENTRY "${_stm32_default_ram_entry}" CACHE STRING
        "RAM image execution address" FORCE)
endif()
set(STM32_RAM_STACK "$ENV{STM32_RAM_STACK}" CACHE STRING
    "Initial main stack pointer" FORCE)
if(NOT STM32_RAM_STACK)
    if(APP_TARGET IN_LIST UAI_KERNEL_APPS OR APP_TARGET STREQUAL "experiment-ai")
        set(_stm32_default_ram_stack "0x34100000")
    else()
        set(_stm32_default_ram_stack "0x34200000")
    endif()
    set(STM32_RAM_STACK "${_stm32_default_ram_stack}" CACHE STRING
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
    else()
        uai_add_missing_tool_target(ram-run
            "ram-run requires STM32_PROGRAMMER_CLI and a built RAM image"
        )
    endif()

    if(STM32_PROGRAMMER_CLI AND STM32_EXTERNAL_LOADER)
        separate_arguments(_stm32_program_extra_args NATIVE_COMMAND
            "${STM32_PROGRAM_EXTRA_ARGS}")
        if((app_target STREQUAL "experiment-ai" OR
            app_target STREQUAL "ai-app") AND
           TARGET ${app_target}-flash-images)
            # Both AI applications are STM32N6 LRUN image sets. The FSBL, signed
            # application, command blobs, and model weights occupy separate
            # external-NOR ranges and must all be present for reset boot.
            set(_ai_flash_dir
                "${CMAKE_BINARY_DIR}/userspace/${app_target}")
            set(_ai_model_dir
                "${CMAKE_SOURCE_DIR}/userspace/${app_target}/models")
            set(_ai_fsbl_image
                "${CMAKE_SOURCE_DIR}/userspace/${app_target}/fsbl/stm32n6570-dk-ai_fsbl.hex")
            if(NOT EXISTS "${_ai_fsbl_image}")
                message(FATAL_ERROR
                    "experiment-ai official FSBL is missing: ${_ai_fsbl_image}")
            endif()
            add_custom_target(program
                DEPENDS ${app_target}-flash-images ${app_target}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_fsbl_image}" -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_flash_dir}/${app_target}-flash.bin"
                           0x70100000 -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_model_dir}/person/network_data.hex" -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_model_dir}/segmentation/network_data.hex" -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_model_dir}/face/network_data.hex" -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_flash_dir}/network_blobs_person.hex" -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_flash_dir}/network_blobs_segmentation.hex" -v
                        ${_stm32_program_extra_args}
                COMMAND ${CMAKE_COMMAND} -E env ${_stm32_cli_environment}
                        "${STM32_PROGRAMMER_CLI}"
                        -c "${_programmer_connection}"
                        -el "${STM32_EXTERNAL_LOADER}"
                        -w "${_ai_flash_dir}/network_blobs_face.hex" -v
                        ${_stm32_program_extra_args}
                USES_TERMINAL
                VERBATIM)
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
