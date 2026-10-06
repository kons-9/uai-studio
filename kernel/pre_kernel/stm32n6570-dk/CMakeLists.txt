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
# The installed newer HAL removed the automatic DDTR bit handling from
# HAL_XSPI_Command(), while the MX66UW1G45G component used by both BSPs still
# relies on it for OPI-DTR status/configuration commands.  Keep the ref XSPI
# HAL implementation until the component and HAL are updated as a pair.
list(FILTER CUBEMX_HAL_SOURCES EXCLUDE REGEX "/stm32n6xx_hal_xspi\\.c$")
set(UAI_LEGACY_XSPI_SOURCE
    "${CMAKE_SOURCE_DIR}/ref/STM32N6_Survivor_Detection/STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c")
if(NOT EXISTS "${UAI_LEGACY_XSPI_SOURCE}")
    set(UAI_LEGACY_XSPI_SOURCE
        "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c")
endif()
if(NOT EXISTS "${UAI_LEGACY_XSPI_SOURCE}")
    message(FATAL_ERROR "No STM32N6 XSPI HAL source was found")
endif()
list(APPEND CUBEMX_HAL_SOURCES "${UAI_LEGACY_XSPI_SOURCE}")
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
    ram_entry.S
    ram_entry.c
    cubemx_entry.c
)

if(APP_TARGET STREQUAL "experiment-camera-lcd" OR
   APP_TARGET STREQUAL "experiment-camera-pipe2" OR
   APP_TARGET STREQUAL "experiment-camera-lcd-touch")
    target_include_directories(stm32n6570_dk BEFORE PUBLIC
        "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc")
endif()

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
        "${STM32CUBE_N6_DIR}/Drivers/BSP/Components/mx66uw1g45g"
        "${STM32CUBE_N6_DIR}/Drivers/BSP/STM32N6570-DK"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2"
        "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/config"
        "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/include"
        "${CMAKE_SOURCE_DIR}/kernel/utkernel/mtk3_bsp2/mtkernel/include"
)

if(NOT APP_TARGET STREQUAL "experiment-camera-lcd" AND
   NOT APP_TARGET STREQUAL "experiment-camera-pipe2" AND
   NOT APP_TARGET STREQUAL "experiment-camera-lcd-touch")
    set(_ai_board_config_app experiment-ai)
    if(APP_TARGET STREQUAL "ai-app")
        set(_ai_board_config_app ai-app)
    endif()
    target_include_directories(stm32n6570_dk PUBLIC
        "${CMAKE_SOURCE_DIR}/userspace/${_ai_board_config_app}/src/board"
        "${CMAKE_SOURCE_DIR}/userspace/${_ai_board_config_app}/config")
endif()

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

if(APP_TARGET STREQUAL "experiment-ai" OR
   APP_TARGET STREQUAL "ai-app" OR
   APP_TARGET STREQUAL "experiment-camera-lcd" OR
   APP_TARGET STREQUAL "experiment-camera-pipe2" OR
   APP_TARGET STREQUAL "experiment-camera-lcd-touch")
    target_compile_definitions(stm32n6570_dk PRIVATE
        UAI_CAMERA_LCD_CLOCKS)
endif()

target_compile_options(stm32n6570_dk PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>
)

add_library(uai::stm32n6570_dk ALIAS stm32n6570_dk)

if(APP_TARGET STREQUAL "experiment-ai")
    # The normal board object above embeds the CubeMX FSBL startup into the
    # RAM-loaded application.  Flash boot needs a separate standalone FSBL:
    # BootROM starts it from XSPI2, and it copies the LRUN application from
    # 0x70100000 to internal RAM before jumping to its vector table.
    set(AI_FSBL_SOURCES
        "${CUBEMX_FSBL_ROOT}/Core/Src/main.c"
        "${CUBEMX_FSBL_ROOT}/Core/Src/extmem_manager.c"
        "${CUBEMX_FSBL_ROOT}/Core/Src/stm32n6xx_hal_msp.c"
        "${CUBEMX_FSBL_ROOT}/Core/Src/stm32n6xx_it.c"
        "${CUBEMX_FSBL_ROOT}/Core/Src/system_stm32n6xx_fsbl.c"
        "${CUBEMX_FSBL_ROOT}/Core/Src/syscalls.c"
        "${CUBEMX_FSBL_ROOT}/Core/Src/sysmem.c"
        "${CUBEMX_FSBL_ROOT}/Core/Startup/startup_stm32n657x0hxq_fsbl.s"
        flash_lrun_size.c
        ${CUBEMX_HAL_SOURCES}
        ${CUBEMX_EXTMEM_SOURCES})

    add_executable(experiment-ai-fsbl.elf ${AI_FSBL_SOURCES})
    target_include_directories(experiment-ai-fsbl.elf PRIVATE
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
        "${STM32CUBE_N6_DIR}/Drivers/BSP/Components/mx66uw1g45g"
        "${STM32CUBE_N6_DIR}/Drivers/BSP/STM32N6570-DK")
    target_compile_definitions(experiment-ai-fsbl.elf PRIVATE
        STM32N6570_DK
        STM32N6570_DK_DEVELOPMENT_MODE
        USE_HAL_DRIVER
        STM32N657xx
        USE_FULL_LL_DRIVER
        CPU_IN_SECURE_STATE
        USER_TZ_SAU_SETUP
        float_t=float)
    target_compile_options(experiment-ai-fsbl.elf PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-mcmse>)
    target_link_libraries(experiment-ai-fsbl.elf PRIVATE m)

    set(AI_FSBL_LINKER_SCRIPT
        "${CUBEMX_FSBL_ROOT}/STM32N657X0HXQ_AXISRAM2_fsbl.ld")
    # CubeMX emits the newer READONLY output-section syntax, while this
    # project intentionally uses the installed GCC 10.3 toolchain.  Keep the
    # generated linker script untouched and derive a compatible build copy.
    set(AI_FSBL_GCC_LINKER_SCRIPT
        "${CMAKE_CURRENT_BINARY_DIR}/experiment-ai-fsbl-gcc.ld")
    file(READ "${AI_FSBL_LINKER_SCRIPT}" _ai_fsbl_ld_contents)
    string(REPLACE "(READONLY)" "" _ai_fsbl_ld_contents
                   "${_ai_fsbl_ld_contents}")
    file(WRITE "${AI_FSBL_GCC_LINKER_SCRIPT}" "${_ai_fsbl_ld_contents}")
    target_link_options(experiment-ai-fsbl.elf PRIVATE
        "-T${AI_FSBL_GCC_LINKER_SCRIPT}"
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/experiment-ai-fsbl.map"
        "-Wl,--print-memory-usage")
    set_target_properties(experiment-ai-fsbl.elf PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/userspace/experiment-ai"
        LINK_DEPENDS
            "${AI_FSBL_LINKER_SCRIPT};${AI_FSBL_GCC_LINKER_SCRIPT}")
    add_custom_command(TARGET experiment-ai-fsbl.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                "$<TARGET_FILE:experiment-ai-fsbl.elf>"
                "$<TARGET_FILE_DIR:experiment-ai-fsbl.elf>/experiment-ai-fsbl.bin"
        COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:experiment-ai-fsbl.elf>"
        VERBATIM)
endif()
