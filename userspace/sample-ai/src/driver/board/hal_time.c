#include <stdint.h>

#include <tk/tkernel.h>

/* The micro T-Kernel owns SysTick, so the Cube HAL's uwTick is not advanced
 * by HAL_IncTick. Use the kernel millisecond clock for HAL timeouts instead. */
uint32_t HAL_GetTick(void)
{
    SYSTIM time = {0};
    return tk_get_tim(&time) == E_OK ? time.lo : 0U;
}

void HAL_Delay(uint32_t delay_ms)
{
    if (delay_ms != 0U) {
        (void)tk_dly_tsk(delay_ms);
    }
}
