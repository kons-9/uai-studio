/* CPU state sanitation that must run before CubeMX's C startup. */

#include "stm32n6xx.h"

__attribute__((section(".text.uai_ram_entry"), noinline))
void uai_prepare_ram_launch(void)
{
    __disable_irq();

    /* Keep exceptions pointed at this RAM image while the generated startup
     * initializes the system. */
    SCB->VTOR = 0x34000400UL;
    __DSB();
    __ISB();

    /* A RAM launch is not a reset. The debugger has already written the new
     * image to SRAM, so cleaning dirty lines from the previous image would
     * write stale data back over it. Invalidate first, then disable. */
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_InvalidateDCache();
        SCB_DisableDCache();
    }
    if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U) {
        SCB_DisableICache();
    }
    SCB_InvalidateICache();

}
