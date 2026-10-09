MEMORY
{
  MODEL_ACTIVATIONS (rw) : ORIGIN = 0x90400000, LENGTH = 0x800000
  MODEL_WEIGHTS (rw) : ORIGIN = 0x91200000, LENGTH = 0x140000
  MODEL_BLOB (rw) : ORIGIN = 0x91340000, LENGTH = 0x40000
  MODEL_SCRATCH (rw) : ORIGIN = 0x342e0000, LENGTH = 0xe0000
}
SECTIONS
{
  .experiment_activations 0x90400000 (NOLOAD) : ALIGN(64)
  {
    __experiment_activations_start__ = .;
    . += 0x800000;
    __experiment_activations_end__ = .;
  } > MODEL_ACTIVATIONS
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
ASSERT(SIZEOF(.experiment_weights) <= 0x140000, "experiment weights overflow")
ASSERT(SIZEOF(.experiment_blob) <= 0x40000, "experiment blob overflow")
