MEMORY
{
  PSRAM_TEST (rw) : ORIGIN = 0x91000000, LENGTH = 4096
}

SECTIONS
{
  .experiment_scratch 0x91000000 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.experiment_scratch))
  } >PSRAM_TEST
}
INSERT AFTER .bss;
ASSERT(SIZEOF(.experiment_scratch) == 4096, "experiment scratch must reserve exactly 4096 bytes")