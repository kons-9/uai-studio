/*
 * experiment-ui-control local camera runtime image.
 *
 * The default dual profile keeps two 400x480 RGB565 buffers in SRAM3/4.
 * The single and fixed-PSRAM profiles do not place their capture buffers in
 * this section; the latter uses the board's external-memory startup.
 */

ENTRY(Reset_Handler)

_estack = 0x34200000;
_Min_Stack_Size = 0x800;
_sstack = _estack - _Min_Stack_Size;

MEMORY
{
  SRAM (rwx)     : ORIGIN = 0x34000400, LENGTH = 0x1FFC00
  SRAM3_4 (rwx)  : ORIGIN = 0x34200000, LENGTH = 0xE0000
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(1024);
  } >SRAM

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
  } >SRAM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >SRAM

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >SRAM

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >SRAM

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >SRAM

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >SRAM

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >SRAM

  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)
    . = ALIGN(4);
  } >SRAM

  .noncacheable :
  {
    . = ALIGN(64);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(64);
    __enoncacheable = .;
  } >SRAM

  .camera_frame_buffer 0x34210000 (NOLOAD) :
  {
    . = ALIGN(64);
    __scamera_frame_buffer = .;
    KEEP(*(.camera_frame_buffer))
    . = ALIGN(64);
    __ecamera_frame_buffer = .;
  } >SRAM3_4

  .mtk_exctbl (NOLOAD) :
  {
    . = ALIGN(1024);
    *(.mtk_exctbl)
    . = ALIGN(1024);
  } >SRAM

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >SRAM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __HeapBase = .;
    . = . + 0x1000;
    __HeapLimit = .;
  } >SRAM

  .stack_dummy (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + 0x1000;
  } >SRAM

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
