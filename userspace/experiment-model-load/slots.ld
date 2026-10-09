MEMORY
{
  MODEL_WEIGHTS (rw) : ORIGIN = 0x91010000, LENGTH = 0x10000
  MODEL_BLOB (rw) : ORIGIN = 0x91020000, LENGTH = 0x10000
  MODEL_SCRATCH (rw) : ORIGIN = 0x342e0000, LENGTH = 0xe0000
}
SECTIONS
{
  .experiment_weights (NOLOAD) : ALIGN(32)
  {
    __experiment_weights_start__ = .;
    KEEP(*(.experiment_weights))
    __experiment_weights_end__ = .;
  } > MODEL_WEIGHTS
  .experiment_blob (NOLOAD) : ALIGN(32)
  {
    __experiment_blob_start__ = .;
    KEEP(*(.experiment_blob))
    KEEP(*(.model_command_blob))
    __experiment_blob_end__ = .;
  } > MODEL_BLOB
  .experiment_scratch (NOLOAD) : ALIGN(64)
  {
    __experiment_scratch_start__ = .;
    . += 0xe0000;
    __experiment_scratch_end__ = .;
  } > MODEL_SCRATCH
}
INSERT AFTER .bss;
ASSERT(SIZEOF(.experiment_weights) <= 0x10000, "experiment weights overflow")
ASSERT(SIZEOF(.experiment_blob) <= 0x10000, "experiment blob overflow")