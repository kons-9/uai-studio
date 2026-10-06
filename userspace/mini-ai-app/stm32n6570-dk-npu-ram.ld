/*
 * mini-ai-app development image (RAM boot).
 *
 * Executable code and application state live in AXISRAM1 (APP). AXISRAM2-6
 * are left to the generated NPU memory pools. The generated Neural-ART
 * command blob is placed in the memory-mapped NOR region and emitted as a
 * separate image by the build.
 *
 * The MEMORY block and the sample_ai_* reservation sections are placeholders:
 * host_app/auto_static_memory_layout rewrites them from config/*.json when
 * the application is configured.
 */
/* A RAM image can inherit stale CPU cache lines from the previous image.
 * Start through the cache-sanitizing trampoline before entering CubeMX's
 * C runtime startup. */
ENTRY(uai_ram_entry)

MEMORY
{
  APP (rwx) : ORIGIN = 0x34000400, LENGTH = 1023K
  MODEL_FLASH (rx) : ORIGIN = 0x70500000, LENGTH = 0x00E00000
  PSRAM_CAPTURE (rwx) : ORIGIN = 0x91000000, LENGTH = 0x00200000
  PSRAM_DISPLAY (rwx) : ORIGIN = 0x91200000, LENGTH = 0x00200000
  PSRAM_INFERENCE (rwx) : ORIGIN = 0x91400000, LENGTH = 0x00300000
  PSRAM_RAW_DUMP (rwx) : ORIGIN = 0x91700000, LENGTH = 0x00400000
  PSRAM_PIPE2_DROP (rwx) : ORIGIN = 0x91B40000, LENGTH = 0x00100000
  PSRAM_TRACE (rwx) : ORIGIN = 0x91C40000, LENGTH = 0x00008000
  PSRAM_CPU_TRACE (rwx) : ORIGIN = 0x91C48000, LENGTH = 0x00080000
  PSRAM_SCRATCH (rwx) : ORIGIN = 0x91D00000, LENGTH = 0x00100000
  PSRAM_INFERENCE_SOURCE (rwx) : ORIGIN = 0x91E00000, LENGTH = 0x00200000
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

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >APP

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >APP

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >APP

  /* Keep the launch entry away from the normal code range so the first fetch
   * cannot be satisfied by an instruction-cache line from the old image.
   * ram-run jumps to this fixed address (STM32_RAM_ENTRY). */
  .text.uai_ram_entry 0x34060000 :
  {
    . = ALIGN(4);
    KEEP(*(.text.uai_ram_entry))
    . = ALIGN(4);
  } >APP

  .network_blobs_person :
  {
    . = ALIGN(64);
    KEEP(*(.network_blobs_person))
    . = ALIGN(64);
  } >MODEL_FLASH

  .network_blobs :
  {
    . = ALIGN(64);
    KEEP(*(.network_blobs))
    . = ALIGN(64);
  } >MODEL_FLASH

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

  .sample_ai_thread_monitor (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_thread_monitor_start__ = .;
    . = . + 0x00008000;
    __sample_ai_thread_monitor_end__ = .;
  } >PSRAM_TRACE

  .sample_ai_cpu_task_monitor (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_cpu_task_monitor_start__ = .;
    . = . + 0x00080000;
    __sample_ai_cpu_task_monitor_end__ = .;
  } >PSRAM_CPU_TRACE

  __end = .;
  _end = .;
  PROVIDE(end = .);

  /* These sections reserve address ranges only; the external PSRAM is
   * initialized by the application before any of the buffers are used. */
  .sample_ai_capture (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_capture0_start__ = .;
    . = . + 0x00100000;
    __sample_ai_capture0_end__ = .;
    __sample_ai_capture1_start__ = .;
    . = . + 0x00100000;
    __sample_ai_capture1_end__ = .;
  } >PSRAM_CAPTURE

  .sample_ai_display (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_display0_start__ = .;
    . = . + 0x00100000;
    __sample_ai_display0_end__ = .;
    __sample_ai_display1_start__ = .;
    . = . + 0x00100000;
    __sample_ai_display1_end__ = .;
  } >PSRAM_DISPLAY

  .sample_ai_inference (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_inference0_start__ = .;
    . = . + 0x00100000;
    __sample_ai_inference0_end__ = .;
    __sample_ai_inference1_start__ = .;
    . = . + 0x00100000;
    __sample_ai_inference1_end__ = .;
    __sample_ai_inference2_start__ = .;
    . = . + 0x00100000;
    __sample_ai_inference2_end__ = .;
  } >PSRAM_INFERENCE

  .sample_ai_raw_dump (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_raw_dump_start__ = .;
    . = . + 0x00400000;
    __sample_ai_raw_dump_end__ = .;
  } >PSRAM_RAW_DUMP

  .sample_ai_pipe2_drop (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_pipe2_drop_start__ = .;
    . = . + 0x00100000;
    __sample_ai_pipe2_drop_end__ = .;
  } >PSRAM_PIPE2_DROP

  .sample_ai_inference_scratch (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_inference_scratch_start__ = .;
    . = . + 0x00100000;
    __sample_ai_inference_scratch_end__ = .;
  } >PSRAM_SCRATCH

  .sample_ai_inference_source (NOLOAD) :
  {
    . = ALIGN(32);
    __sample_ai_inference_source0_start__ = .;
    . = . + 0x000A8C00;
    __sample_ai_inference_source0_end__ = .;
    __sample_ai_inference_source1_start__ = .;
    . = . + 0x000A8C00;
    __sample_ai_inference_source1_end__ = .;
    __sample_ai_inference_source2_start__ = .;
    . = . + 0x000A8C00;
    __sample_ai_inference_source2_end__ = .;
  } >PSRAM_INFERENCE_SOURCE

  /DISCARD/ :
  {
    *libc.a ( * )
    *libm.a ( * )
    *libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
