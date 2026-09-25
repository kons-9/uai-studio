/*
 * sample-ai development image.
 *
 * The generated Neural-ART command blob is part of .rodata and can be much
 * larger than the small sample-hello-world image.  Keep code, read-only data and the
 * application state in all of AXISRAM1 (1 MiB), leaving AXISRAM2-6 to the
 * generated NPU memory pools.
 */
/* A RAM image can inherit stale CPU cache lines from the previous image.
 * Start through the cache-sanitizing trampoline before entering CubeMX's
 * C runtime startup. */
ENTRY(uai_ram_entry)

MEMORY
{
  APP (rwx) : ORIGIN = 0x34000400, LENGTH = 1023K
}

_estack = ORIGIN(APP) + LENGTH(APP);
_Min_Stack_Size = 0x800;
_sstack = _estack - _Min_Stack_Size;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(1024);
  } >APP

  .text :
  {
    . = ALIGN(4);
    KEEP(*(.text.Reset_Handler))
    *(.text)
    /* ram_entry.S is placed separately below so it is not part of the
     * ordinary code range. */
    *(EXCLUDE_FILE (*ram_entry.S.obj *ram_entry.c.obj) .text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >APP

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >APP

  /* Reserve the 0x34049000 gap after .rodata and before the RAM entry at
   * 0x34050000 for the C runtime callback tables. NPU-on adds command data to
   * .rodata through 0x340486df, so the former 0x34048000 slot overlaps it.
   * Keep a fixed table location for RAM launch startup. */
  .preinit_array 0x34049000 :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >APP

  .init_array 0x34049000 :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >APP

  .fini_array 0x34049010 :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >APP

  /* Keep the launch entry away from the normal code range so the first fetch
   * cannot be satisfied by an instruction-cache line from the old image. */
  .text.uai_ram_entry 0x34050000 :
  {
    . = ALIGN(4);
    KEEP(*(.text.uai_ram_entry))
    . = ALIGN(4);
  } >APP

  .network_blobs 0x34085000 :
  {
    . = ALIGN(64);
    KEEP(*(.network_blobs))
    . = ALIGN(64);
  } >APP

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >APP

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >APP AT>APP

  .noncacheable :
  {
    . = ALIGN(8);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(8);
    __enoncacheable = .;
  } >APP

  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)
    . = ALIGN(4);
  } >APP

  .mtk_exctbl (NOLOAD) :
  {
    . = ALIGN(1024);
    *(.mtk_exctbl)
    . = ALIGN(1024);
  } >APP

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >APP

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    __HeapBase = .;
    . = . + 0x1000;
    __HeapLimit = .;
  } >APP

  .stack_dummy (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + 0x1000;
  } >APP

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
