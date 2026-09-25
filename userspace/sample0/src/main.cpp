#include <tk/tkernel.h>

/* CライブラリのT-Monitor宣言を、C++からC ABIで呼び出す。 */
extern "C" {
#include <tm/tmonitor.h>
}

/*
 * µT-Kernel calls usermain() from its initial task.  Keep this first sample
 * intentionally small: it proves the kernel, scheduler, SysTick and
 * T-Monitor UART are linked together before adding middleware.
 */
/* µT-Kernelが名前 usermain をC ABIで検索して呼ぶエントリーポイント。 */
extern "C" INT usermain(void)
{
    tm_putstring((UB*)"Hello from uai-studio / STM32N6570-DK\n");

    for (;;) {
        tk_dly_tsk(1000);
    }
}
