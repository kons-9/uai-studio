/*
 * STM32N6570-DK development image using the CubeMX-generated FSBL startup.
 *
 * The image is loaded as one RAM image at 0x34000400. CubeMX's FSBL
 * startup expects the vector table at the beginning of the image and calls
 * Reset_Handler at the entry address supplied by STM32CubeProgrammer.
 */

ENTRY(Reset_Handler)

_estack = 0x34200000;
_Min_Stack_Size = 0x800;
_sstack = _estack - _Min_Stack_Size;

MEMORY
{
  ROM (rx)  : ORIGIN = 0x34000400, LENGTH = 511K
  RAM (xrw) : ORIGIN = 0x34080000, LENGTH = 1536K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    /* STM32CubeProgrammer starts this RAM image at 0x34000800. */
    . = ALIGN(1024);
  } >ROM

  .text :
  {
    . = ALIGN(4);
    KEEP(*(.text.Reset_Handler))
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >ROM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >ROM

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >ROM

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >ROM

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >ROM

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >ROM

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>ROM

  .noncacheable :
  {
    . = ALIGN(8);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(8);
    __enoncacheable = .;
  } >RAM

  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)
    . = ALIGN(4);
  } >ROM

  .mtk_exctbl (NOLOAD) :
  {
    . = ALIGN(1024);
    *(.mtk_exctbl)
    . = ALIGN(1024);
  } >RAM

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __HeapBase = .;
    . = . + 0x1000;
    __HeapLimit = .;
  } >RAM

  .stack_dummy (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + 0x1000;
  } >RAM

  __end = .;
  _end = .;
  PROVIDE(end = .);

  /DISCARD/ :
  {
    *libc.a ( * )
    *libm.a ( * )
    *libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
