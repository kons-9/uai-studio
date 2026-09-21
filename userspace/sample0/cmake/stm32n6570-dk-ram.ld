/*
 * STM32N6570-DK development-mode image.
 *
 * STM32N6 has no internal flash.  This first CMake image is loaded into the
 * secure AXI SRAM by a debugger.  The address/size match the BSP2
 * INTERNAL_RAM_* definitions used by the STM32N6570-DK port.
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

MEMORY
{
    RAM (xrw) : ORIGIN = 0x34000400, LENGTH = 0x001FFC00
}

SECTIONS
{
    .isr_vector : ALIGN(1024)
    {
        __vector_org = .;
        KEEP(*(.isr_vector))
        . = ALIGN(1024);
    } >RAM

    .text :
    {
        . = ALIGN(4);
        __start = .;
        *(.text.Reset_Handler)
        *(.text.Default_Handler)
        *(.text .text.*)
        *(.rodata .rodata.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >RAM

    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >RAM

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >RAM

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >RAM

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } >RAM
    __data_org = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        PROVIDE(__noinit_start = .);
        *(.noinit .noinit.*)
        PROVIDE(__noinit_end = .);
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } >RAM

    /* BSP2 copies the original vector table into this RAM table. */
    .mtk_bsp2 (NOLOAD) :
    {
        . = ALIGN(1024);
        KEEP(*(.mtk_exctbl))
        . = ALIGN(4);
    } >RAM

    __end = .;
    _end = .;
    PROVIDE(end = .);

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
