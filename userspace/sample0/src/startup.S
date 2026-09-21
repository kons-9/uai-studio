/* Minimal secure Cortex-M55 startup for the RAM development image. */

.syntax unified
.thumb

.section .isr_vector, "a", %progbits
.align 10
.global __Vectors
.type __Vectors, %object
__Vectors:
    .word _estack
    .word Reset_Handler
    /* 16 system vectors + 196 external vectors, minus SP and Reset. */
    .rept 210
    .word Default_Handler
    .endr
.size __Vectors, . - __Vectors

.section .text.Reset_Handler, "ax", %progbits
.align 2
.global Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_estack
    mov sp, r0

    /* The debugger enters directly at Reset_Handler in development mode. */
    ldr r0, =0xE000ED08       /* SCB->VTOR */
    ldr r1, =__Vectors
    str r1, [r0]
    dsb
    isb

    bl SystemInit

    /* In a RAM image the data is already loaded at its execution address. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    movs r2, #0
1:
    cmp r0, r1
    bcs 2f
    str r2, [r0], #4
    b 1b
2:
    bl main
3:
    wfi
    b 3b
.size Reset_Handler, . - Reset_Handler

.section .text.Default_Handler, "ax", %progbits
.align 2
.global Default_Handler
.type Default_Handler, %function
Default_Handler:
4:
    wfi
    b 4b
.size Default_Handler, . - Default_Handler
