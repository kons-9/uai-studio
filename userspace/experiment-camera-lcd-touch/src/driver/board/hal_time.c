#include <stdint.h>

#include <tk/tkernel.h>

volatile UW uai_systick_count;

/* The application runs after µT-Kernel has taken ownership of the system
 * tick.  Keep the Cube/BSP time API connected to that same monotonic clock. */
uint32_t HAL_GetTick(void)
{
    SYSTIM time = {0};
    if (tk_get_tim(&time) != E_OK) {
        return 0U;
    }
    return time.lo;
}

void HAL_Delay(uint32_t delay_ms)
{
    if (delay_ms != 0U) {
        (void)tk_dly_tsk(delay_ms);
    }
}
