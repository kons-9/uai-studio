cmake_minimum_required(VERSION 3.16)

set(CUBEMX_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cubemx" CACHE PATH
    "Directory containing the CubeMX-generated project")
set(CUBEMX_ROOT "${CUBEMX_OUTPUT_DIR}")
set(CUBEMX_FSBL_ROOT "${CUBEMX_ROOT}/FSBL")
set(UAI_STM32N6570_DK_LINKER_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/stm32n6570-dk-cubemx-ram.ld"
    CACHE FILEPATH "STM32N6570-DK RAM linker script" FORCE)

if(NOT EXISTS "${CUBEMX_FSBL_ROOT}/Core/Src/main.c")
    message(WARNING
        "CubeMX output is missing. Run 'make generate' before building.")
endif()

file(GLOB CUBEMX_HAL_SOURCES CONFIGURE_DEPENDS
    "${CUBEMX_ROOT}/Drivers/STM32N6xx_HAL_Driver/Src/*.c")
file(GLOB CUBEMX_EXTMEM_SOURCES CONFIGURE_DEPENDS
    "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/*.c"
    "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/boot/*.c"
    "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/*.c"
    "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/sal/*.c")

set(CUBEMX_FSBL_SOURCES
    "${CUBEMX_FSBL_ROOT}/Core/Src/extmem_manager.c"
    "${CUBEMX_FSBL_ROOT}/Core/Src/stm32n6xx_hal_msp.c"
    "${CUBEMX_FSBL_ROOT}/Core/Src/stm32n6xx_it.c"
    "${CUBEMX_FSBL_ROOT}/Core/Src/system_stm32n6xx_fsbl.c"
    "${CUBEMX_FSBL_ROOT}/Core/Src/syscalls.c"
    "${CUBEMX_FSBL_ROOT}/Core/Src/sysmem.c"
    "${CUBEMX_FSBL_ROOT}/Core/Startup/startup_stm32n657x0hxq_fsbl.s")

add_library(stm32n6570_dk OBJECT
    ${CUBEMX_FSBL_SOURCES}
    ${CUBEMX_HAL_SOURCES}
    ${CUBEMX_EXTMEM_SOURCES}
    cubemx_entry.c
)

target_include_directories(stm32n6570_dk
    PUBLIC
        "${CUBEMX_FSBL_ROOT}/Core/Inc"
        "${CUBEMX_FSBL_ROOT}/Core/Src"
        "${CUBEMX_ROOT}/Appli/Core/Inc"
        "${CUBEMX_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc"
        "${CUBEMX_ROOT}/Drivers/STM32N6xx_HAL_Driver/Inc/Legacy"
        "${CUBEMX_ROOT}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${CUBEMX_ROOT}/Drivers/CMSIS/Include"
        "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager"
        "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/boot"
        "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/sal"
        "${CUBEMX_ROOT}/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp"
)

target_compile_definitions(stm32n6570_dk PRIVATE
    STM32N6570_DK
    STM32N6570_DK_DEVELOPMENT_MODE
    USE_HAL_DRIVER
    STM32N657xx
    USE_FULL_LL_DRIVER
    USER_TZ_SAU_SETUP
    # GCC 10.3's freestanding newlib math.h omits the C99 float_t typedef,
    # while CubeMX's STM32N6 clock update code uses it.
    float_t=float
)

target_compile_options(stm32n6570_dk PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>
)

add_library(uai::stm32n6570_dk ALIAS stm32n6570_dk)
