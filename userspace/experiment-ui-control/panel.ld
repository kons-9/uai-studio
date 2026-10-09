SECTIONS
{
  .experiment_panel 0x91000000 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.experiment_panel))
  }
}
INSERT AFTER .bss;
ASSERT(SIZEOF(.experiment_panel) <= 0x100000, "experiment panel overflow")