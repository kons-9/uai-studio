/*
 * Project-owned board adapter between CubeMX-generated FSBL code and BSP2.
 * Generated files remain untouched and are rebuilt below the CMake build tree.
 */
#define main cubemx_unused_generated_main
#include "main.c"
#undef main

void knl_start_mtkernel(void);

int main(void)
{
    /* Keep HAL's early SysTick from reaching CubeMX's Default_Handler before
     * BSP2 has installed its exception table. */
    __disable_irq();
    HAL_Init();
    HAL_SuspendTick();

    SystemClock_Config();
    /* HAL_RCC_ClockConfig() re-enables the HAL tick after changing clocks. */
    HAL_SuspendTick();
    PeriphCommonClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    __enable_irq();
    knl_start_mtkernel();
    return 0;
}
