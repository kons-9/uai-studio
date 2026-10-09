SECTIONS
{
  .experiment_weights 0x91010000 (NOLOAD) : ALIGN(32) { KEEP(*(.experiment_weights)) }
  .experiment_blob 0x91020000 (NOLOAD) : ALIGN(32) { KEEP(*(.experiment_blob)) }
}
INSERT AFTER .bss;
ASSERT(SIZEOF(.experiment_weights) <= 0x10000, "experiment weights overflow")
ASSERT(SIZEOF(.experiment_blob) <= 0x10000, "experiment blob overflow")