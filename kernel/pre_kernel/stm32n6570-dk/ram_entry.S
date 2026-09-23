/* Project-owned RAM-launch entry point.
 *
 * The CubeMX Reset_Handler is reached only after the C runtime has already
 * been entered. That is too late to discard cache state inherited from the
 * previous RAM image, so this trampoline runs from a relocated address and
 * sanitizes the CPU before jumping to the generated startup. */

.syntax unified
.arch armv8.1-m.main
.fpu softvfp
.thumb

.section .text.uai_ram_entry, "ax", %progbits
.align 2
.global uai_ram_entry
.type uai_ram_entry, %function
.extern _estack
.extern _sstack
.extern uai_prepare_ram_launch
.extern Reset_Handler

uai_ram_entry:
    cpsid i
    movs  r0, #0
    msr   MSPLIM, r0
    msr   MSPLIM_NS, r0
    ldr   r0, =_estack
    mov   sp, r0
    bl    uai_prepare_ram_launch
    b     Reset_Handler

.size uai_ram_entry, .-uai_ram_entry
