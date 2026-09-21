#include <tk/tkernel.h>

extern "C" {
#include <tm/tmonitor.h>
}

/*
 * µT-Kernel calls usermain() from its initial task.  Keep this first sample
 * intentionally small: it proves the kernel, scheduler, SysTick and
 * T-Monitor UART are linked together before adding middleware.
 */
extern "C" INT usermain(void)
{
    tm_putstring((UB*)"Hello from uai-studio / STM32N6570-DK\n");

    for (;;) {
        tk_dly_tsk(1000);
    }
}
