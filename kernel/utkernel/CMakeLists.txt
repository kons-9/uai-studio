cmake_minimum_required(VERSION 3.16)

set(MTK3_BSP2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/mtk3_bsp2")
set(MTKERNEL_ROOT "${MTK3_BSP2_ROOT}/mtkernel")

# BSP2's STM32Cube device header includes the family HAL header even when all
# optional HAL device drivers are disabled.  A full STM32CubeN6 checkout can
# be supplied later with -DSTM32CUBE_N6_DIR=..., while experiment-hello-world keeps a tiny
# compile-only compatibility header so the kernel itself remains buildable.
set(STM32CUBE_N6_DIR "" CACHE PATH "STM32CubeN6 package root")
if(STM32CUBE_N6_DIR)
    if(NOT EXISTS
       "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc/stm32n6xx_hal.h")
        message(FATAL_ERROR
            "STM32CUBE_N6_DIR does not contain the STM32N6 HAL headers: "
            "${STM32CUBE_N6_DIR}")
    endif()
    set(STM32N6_HAL_INCLUDE_DIR
        "${STM32CUBE_N6_DIR}/Drivers/STM32N6xx_HAL_Driver/Inc")
    message(STATUS "Using STM32CubeN6 HAL: ${STM32N6_HAL_INCLUDE_DIR}")
else()
    set(STM32N6_HAL_INCLUDE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../cmake/stm32n6-minimal/include")
    message(STATUS
        "Using the minimal STM32N6 HAL compatibility header; "
        "set STM32CUBE_N6_DIR to enable the full STM32CubeN6 package")
endif()

if(NOT EXISTS "${MTKERNEL_ROOT}/kernel/tkernel/tkinit.c")
    message(FATAL_ERROR
        "The BSP2 mtkernel submodule is not initialized. Run: "
        "git -C kernel/utkernel/mtk3_bsp2 submodule update --init")
endif()

# Architecture-independent µT-Kernel sources.  These are deliberately
# selected by subsystem; no platform Makefile is involved.
file(GLOB MTKERNEL_TKERNEL_SOURCES CONFIGURE_DEPENDS
     "${MTKERNEL_ROOT}/kernel/tkernel/*.c")
file(GLOB MTKERNEL_TSTDLIB_SOURCES CONFIGURE_DEPENDS
     "${MTKERNEL_ROOT}/kernel/tstdlib/*.c")

set(MTKERNEL_COMMON_SOURCES
    ${MTKERNEL_TKERNEL_SOURCES}
    ${MTKERNEL_TSTDLIB_SOURCES}
    ${MTKERNEL_ROOT}/kernel/sysinit/sysinit.c
    ${MTKERNEL_ROOT}/kernel/inittask/inittask.c
    ${MTKERNEL_ROOT}/kernel/usermain/usermain.c
    ${MTKERNEL_ROOT}/lib/libtk/fastlock.c
    ${MTKERNEL_ROOT}/lib/libtk/fastmlock.c
    ${MTKERNEL_ROOT}/lib/libtk/kmalloc.c
    ${MTKERNEL_ROOT}/lib/libtm/libtm.c
    ${MTKERNEL_ROOT}/lib/libtm/libtm_printf.c
    ${MTKERNEL_ROOT}/device/common/drvif/msdrvif.c
    ${MTKERNEL_ROOT}/device/adc/adc.c
    ${MTKERNEL_ROOT}/device/i2c/i2c.c
    ${MTKERNEL_ROOT}/device/ser/ser.c
)

# STM32Cube/BSP2 ARMv8-M and STM32N6 sources for STM32N6570-DK.
set(MTKERNEL_STM32N657_SOURCES
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/cpu/core/armv8m/cpu_cntl.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/cpu/core/armv8m/dispatch.S
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/cpu/core/armv8m/exc_hdr.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/cpu/core/armv8m/interrupt.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/cpu/core/armv8m/sys_start.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/devinit.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/hw_setting.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/power_save.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/lib/libtk/cpu/core/armv8m/int_armv8m.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/lib/libtk/cpu/core/armv8m/wusec_armv8m.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/lib/libtk/cpu/stm32n6/int_stm32n6.c
    ${MTK3_BSP2_ROOT}/sysdepend/stm32_cube/lib/libtm/discovery_stm32n657/tm_com.c
)

foreach(_source IN LISTS MTKERNEL_COMMON_SOURCES MTKERNEL_STM32N657_SOURCES)
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "µT-Kernel source does not exist: ${_source}")
    endif()
endforeach()

add_library(utkernel STATIC
    ${MTKERNEL_COMMON_SOURCES}
    ${MTKERNEL_STM32N657_SOURCES}
)

target_include_directories(utkernel
    PUBLIC
        "${MTK3_BSP2_ROOT}"
        "${MTK3_BSP2_ROOT}/config"
        "${MTK3_BSP2_ROOT}/include"
        "${MTKERNEL_ROOT}/include"
    PRIVATE
        "${MTKERNEL_ROOT}/kernel/knlinc"
        "${CUBEMX_OUTPUT_DIR}/FSBL/Core/Inc"
        "${CUBEMX_OUTPUT_DIR}/Appli/Core/Inc"
        "${STM32N6_HAL_INCLUDE_DIR}"
)

if(STM32CUBE_N6_DIR)
    target_include_directories(utkernel PRIVATE
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Device/ST/STM32N6xx/Include"
        "${STM32CUBE_N6_DIR}/Drivers/CMSIS/Include")
    target_compile_definitions(utkernel PRIVATE STM32N657xx)
endif()

target_compile_definitions(utkernel
    PUBLIC
        _STM32CUBE_DISCOVERY_N657_
        # The STM32N6 BSP2 ARMv8-M source contains a lowercase typo.  Keep the
        # vendor checkout untouched and provide the intended compatibility
        # alias at the target boundary.
        sN_INTVEC=N_INTVEC
        # BSP2 enters the kernel through knl_main().  kons-9/mtkernel_3
        # v3.00.08 renamed the opt-in macro used by sysinit.c; enable its
        # compatibility path without modifying the submodule checkout.
        ADD_PREFIX_MAIN_FUNC=1
)

target_compile_options(utkernel
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
)

set_target_properties(utkernel PROPERTIES
    ARCHIVE_OUTPUT_NAME utkernel
)

add_library(uai::utkernel ALIAS utkernel)
