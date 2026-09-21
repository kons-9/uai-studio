# GNU Arm Embedded toolchain for the STM32N6570-DK.
#
# The N6570-DK uses the Cortex-M55 core.  The default image is linked into
# secure AXI SRAM so it can be loaded directly by a debugger (development
# mode).  External-flash/XIP packaging can be added later without changing
# the µT-Kernel source target.

if(NOT DEFINED ARM_NONE_EABI_TOOLCHAIN_PATH)
    if(DEFINED ENV{ARM_NONE_EABI_TOOLCHAIN_PATH})
        set(ARM_NONE_EABI_TOOLCHAIN_PATH
            "$ENV{ARM_NONE_EABI_TOOLCHAIN_PATH}"
            CACHE PATH "Root of the GNU Arm Embedded toolchain")
    else()
        set(ARM_NONE_EABI_TOOLCHAIN_PATH
            ""
            CACHE PATH "Root of the GNU Arm Embedded toolchain")
    endif()
endif()

if(ARM_NONE_EABI_TOOLCHAIN_PATH STREQUAL "")
    find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc)
    find_program(ARM_NONE_EABI_GXX arm-none-eabi-g++)
    find_program(ARM_NONE_EABI_OBJCOPY arm-none-eabi-objcopy)
    find_program(ARM_NONE_EABI_SIZE arm-none-eabi-size)
else()
    set(ARM_NONE_EABI_GCC
        "${ARM_NONE_EABI_TOOLCHAIN_PATH}/bin/arm-none-eabi-gcc")
    set(ARM_NONE_EABI_GXX
        "${ARM_NONE_EABI_TOOLCHAIN_PATH}/bin/arm-none-eabi-g++")
    set(ARM_NONE_EABI_OBJCOPY
        "${ARM_NONE_EABI_TOOLCHAIN_PATH}/bin/arm-none-eabi-objcopy")
    set(ARM_NONE_EABI_SIZE
        "${ARM_NONE_EABI_TOOLCHAIN_PATH}/bin/arm-none-eabi-size")
endif()

if(NOT ARM_NONE_EABI_GCC OR NOT ARM_NONE_EABI_GXX OR
   NOT ARM_NONE_EABI_OBJCOPY OR NOT ARM_NONE_EABI_SIZE)
    message(FATAL_ERROR
        "The GNU Arm Embedded toolchain (gcc, g++, objcopy, size) was not "
        "found. Install it or set ARM_NONE_EABI_TOOLCHAIN_PATH.")
endif()

set(CMAKE_SYSTEM_NAME Generic CACHE STRING "Target system name" FORCE)
set(CMAKE_SYSTEM_PROCESSOR arm CACHE STRING "Target processor" FORCE)
set(CMAKE_C_COMPILER "${ARM_NONE_EABI_GCC}" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${ARM_NONE_EABI_GXX}" CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_ASM_COMPILER "${ARM_NONE_EABI_GCC}" CACHE FILEPATH "ASM compiler" FORCE)
set(CMAKE_OBJCOPY "${ARM_NONE_EABI_OBJCOPY}" CACHE FILEPATH "Binary converter" FORCE)
set(CMAKE_SIZE "${ARM_NONE_EABI_SIZE}" CACHE FILEPATH "Size utility" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(STM32N657_CPU_FLAGS
    "-mcpu=cortex-m55 -mthumb -mfloat-abi=hard")
set(STM32N657_COMMON_FLAGS
    "${STM32N657_CPU_FLAGS} -ffreestanding -fno-common -ffunction-sections -fdata-sections -Wall -Wextra")

set(CMAKE_C_FLAGS
    "${STM32N657_COMMON_FLAGS}"
    CACHE STRING "C compiler flags" FORCE)
set(CMAKE_CXX_FLAGS
    "${STM32N657_COMMON_FLAGS} -fno-exceptions -fno-rtti"
    CACHE STRING "C++ compiler flags" FORCE)
set(CMAKE_ASM_FLAGS
    "${STM32N657_CPU_FLAGS} -x assembler-with-cpp"
    CACHE STRING "ASM compiler flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS
    "-specs=nosys.specs -Wl,--gc-sections"
    CACHE STRING "Executable linker flags" FORCE)
