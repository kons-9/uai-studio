#include <stdint.h>

/* STM32N6 starts from the 64 MHz HSI after reset. */
extern "C" {

uint32_t SystemCoreClock = 64000000U;

/*
 * The first image is intended to run after the STM32N6 secure boot/FSBL
 * setup, just like a Cube-generated secure application.  Keep this hook in
 * the application so a later CMSIS/HAL SystemInit can replace it without
 * changing the µT-Kernel target.
 */
void SystemInit(void)
{
}

void knl_start_mtkernel(void);

int main(void)
{
    knl_start_mtkernel();
    return 0;
}

}
