/*
 * Project-owned board adapter between CubeMX-generated FSBL code and BSP2.
 * Generated files remain untouched and are rebuilt below the CMake build tree.
 */
#define main cubemx_unused_generated_main
#include "main.c"
#undef main

void knl_start_mtkernel(void);

static void sample2_system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK ||
        HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) !=
            HAL_OK) {
        Error_Handler();
    }

    /* Match the ref application's PLL outputs.  In particular, PLL2 feeds
     * DCMIPP (IC17), while PLL1 feeds the CSI receiver (IC18).  The generated
     * FSBL clock setup left PLL2 disabled and used a different PLL1 rate. */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSIDiv = RCC_HSI_DIV1;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;

    oscillator.PLL1.PLLState = RCC_PLL_ON;
    oscillator.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL1.PLLM = 2U;
    oscillator.PLL1.PLLN = 25U;
    oscillator.PLL1.PLLFractional = 0U;
    oscillator.PLL1.PLLP1 = 1U;
    oscillator.PLL1.PLLP2 = 1U;

    oscillator.PLL2.PLLState = RCC_PLL_ON;
    oscillator.PLL2.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL2.PLLM = 8U;
    oscillator.PLL2.PLLN = 125U;
    oscillator.PLL2.PLLFractional = 0U;
    oscillator.PLL2.PLLP1 = 1U;
    oscillator.PLL2.PLLP2 = 1U;

    oscillator.PLL3.PLLState = RCC_PLL_ON;
    oscillator.PLL3.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL3.PLLM = 8U;
    oscillator.PLL3.PLLN = 225U;
    oscillator.PLL3.PLLFractional = 0U;
    oscillator.PLL3.PLLP1 = 1U;
    oscillator.PLL3.PLLP2 = 2U;

    oscillator.PLL4.PLLState = RCC_PLL_ON;
    oscillator.PLL4.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL4.PLLM = 8U;
    oscillator.PLL4.PLLN = 225U;
    oscillator.PLL4.PLLFractional = 0U;
    oscillator.PLL4.PLLP1 = 6U;
    oscillator.PLL4.PLLP2 = 6U;

    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
        Error_Handler();
    }

    clocks.ClockType = RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 |
                       RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 |
                       RCC_CLOCKTYPE_PCLK5;
    clocks.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
    clocks.AHBCLKDivider = RCC_HCLK_DIV2;
    clocks.APB1CLKDivider = RCC_APB1_DIV1;
    clocks.APB2CLKDivider = RCC_APB2_DIV1;
    clocks.APB4CLKDivider = RCC_APB4_DIV1;
    clocks.APB5CLKDivider = RCC_APB5_DIV1;
    clocks.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clocks.IC1Selection.ClockDivider = 1U;
    clocks.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clocks.IC2Selection.ClockDivider = 2U;
    clocks.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL2;
    clocks.IC6Selection.ClockDivider = 1U;
    clocks.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL3;
    clocks.IC11Selection.ClockDivider = 1U;

    if (HAL_RCC_ClockConfig(&clocks) != HAL_OK) {
        Error_Handler();
    }
}

int main(void)
{
    /* Keep HAL's early SysTick from reaching CubeMX's Default_Handler before
     * BSP2 has installed its exception table. */
    __disable_irq();
    HAL_Init();
    HAL_SuspendTick();

    sample2_system_clock_config();
    HAL_SuspendTick();
    PeriphCommonClock_Config();
    MX_GPIO_Init();
    MX_CACHEAXI_Init();
    MX_RAMCFG_Init();
    MX_USART1_UART_Init();
    MX_XSPI1_Init();
    MX_XSPI2_Init();
    SystemIsolation_Config();

    __enable_irq();
    knl_start_mtkernel();
    return 0;
}
