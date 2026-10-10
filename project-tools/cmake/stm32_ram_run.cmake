if(NOT DEFINED STM32_RAM_NM OR NOT DEFINED STM32_RAM_ELF OR
   NOT DEFINED STM32_RAM_ENTRY_SYMBOL OR
   NOT DEFINED STM32_PROGRAMMER_CLI OR
   NOT DEFINED STM32_PROGRAMMER_CONNECTION OR
   NOT DEFINED STM32_RAM_IMAGE OR NOT DEFINED STM32_RAM_ADDRESS OR
   NOT DEFINED STM32_RAM_STACK OR NOT DEFINED STM32_RAM_XPSR)
    message(FATAL_ERROR "Missing STM32 RAM launch configuration")
endif()

execute_process(
    COMMAND "${STM32_RAM_NM}" -n "${STM32_RAM_ELF}"
    RESULT_VARIABLE _nm_result
    OUTPUT_VARIABLE _nm_output
    ERROR_VARIABLE _nm_error
)
if(NOT _nm_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect RAM image ELF: ${_nm_error}")
endif()

string(REPLACE "\r\n" "\n" _nm_output "${_nm_output}")
string(REPLACE "\n" ";" _nm_lines "${_nm_output}")
set(_entry_address "")
foreach(_nm_line IN LISTS _nm_lines)
    if(_nm_line MATCHES "^[ \t]*([0-9A-Fa-f]+)[ \t]+T[ \t]+${STM32_RAM_ENTRY_SYMBOL}$")
        set(_entry_address "${CMAKE_MATCH_1}")
        break()
    endif()
endforeach()
if(_entry_address STREQUAL "")
    message(FATAL_ERROR
        "Strong RAM entry symbol '${STM32_RAM_ENTRY_SYMBOL}' was not found in ${STM32_RAM_ELF}")
endif()

# Cortex-M starts Thumb code with bit 0 set. nm reports the aligned symbol
# address, so set that bit before handing PC to STM32CubeProgrammer. Keep the
# value hexadecimal because the CLI's coreReg parser rejects decimal addresses.
string(TOLOWER "${_entry_address}" _entry_address)
string(LENGTH "${_entry_address}" _entry_address_length)
math(EXPR _entry_last_index "${_entry_address_length} - 1")
string(SUBSTRING "${_entry_address}" 0 ${_entry_last_index} _entry_pc_prefix)
string(SUBSTRING "${_entry_address}" ${_entry_last_index} 1 _entry_last_nibble)
if(_entry_last_nibble STREQUAL "0")
    set(_entry_pc_nibble "1")
elseif(_entry_last_nibble STREQUAL "2")
    set(_entry_pc_nibble "3")
elseif(_entry_last_nibble STREQUAL "4")
    set(_entry_pc_nibble "5")
elseif(_entry_last_nibble STREQUAL "6")
    set(_entry_pc_nibble "7")
elseif(_entry_last_nibble STREQUAL "8")
    set(_entry_pc_nibble "9")
elseif(_entry_last_nibble STREQUAL "a")
    set(_entry_pc_nibble "b")
elseif(_entry_last_nibble STREQUAL "c")
    set(_entry_pc_nibble "d")
elseif(_entry_last_nibble STREQUAL "e")
    set(_entry_pc_nibble "f")
else()
    message(FATAL_ERROR
        "RAM entry symbol '${STM32_RAM_ENTRY_SYMBOL}' is not halfword-aligned: 0x${_entry_address}")
endif()
set(_entry_pc "0x${_entry_pc_prefix}${_entry_pc_nibble}")
message(STATUS
    "Loading ${STM32_RAM_IMAGE} at ${STM32_RAM_ADDRESS}; ${STM32_RAM_ENTRY_SYMBOL}=0x${_entry_address} (PC=${_entry_pc})")
execute_process(
    COMMAND "${STM32_PROGRAMMER_CLI}"
        -c "${STM32_PROGRAMMER_CONNECTION}"
        -halt
        -d "${STM32_RAM_IMAGE}" "${STM32_RAM_ADDRESS}"
        -v
        -coreReg "MSP=${STM32_RAM_STACK}"
                 "PC=${_entry_pc}"
                 "XPSR=${STM32_RAM_XPSR}"
        -run
    RESULT_VARIABLE _programmer_result
)
if(NOT _programmer_result EQUAL 0)
    message(FATAL_ERROR "STM32CubeProgrammer RAM launch failed: ${_programmer_result}")
endif()
