/*
 * Project-owned board adapter between CubeMX-generated FSBL code and BSP2.
 * Generated files remain untouched and are rebuilt below the CMake build tree.
 */
#define main cubemx_unused_generated_main
#include "main.c"
#undef main

#include "stm32n6570_discovery_xspi.h"
#include <stdio.h>
#include <string.h>

void knl_start_mtkernel(void);
extern XSPI_HandleTypeDef hxspi2;
extern volatile int32_t uai_nor_bsp_stage;
extern volatile int32_t uai_nor_reset_stage;
extern volatile int32_t uai_nor_reset_result;
extern volatile uint32_t uai_nor_reset_hal_error;
extern volatile uint32_t uai_nor_reset_sr;
extern volatile uint32_t uai_nor_reset_cr;
extern volatile uint32_t uai_nor_sr_after_init;
extern volatile uint32_t uai_nor_cr_after_init;

static void cubemx_debug_puts(const char *message)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)message,
                            (uint16_t)strlen(message), 1000U);
}

int cubemx_initialize_external_memory(void)
{
    /* BSP_XSPI_RAM_Init() resets XSPIM. The reference app initializes NOR
     * through the board BSP after RAM, which restores Port 2 routing, resets
     * the flash from its current mode, and enters OPI-DTR in a known sequence. */
    BSP_XSPI_NOR_Init_t nor_init = {0};
    nor_init.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
    nor_init.TransferRate = BSP_XSPI_NOR_DTR_TRANSFER;

    const int32_t init_status = BSP_XSPI_NOR_Init(0U, &nor_init);
    char message[160];
    (void)snprintf(message, sizeof(message),
                   "boot: nor bsp init=%ld stage=%ld reset=%ld/%ld err=%lx sr=%lx cr=%lx iom=%lx\r\n",
                   (long)init_status, (long)uai_nor_bsp_stage,
                   (long)uai_nor_reset_stage, (long)uai_nor_reset_result,
                   (unsigned long)uai_nor_reset_hal_error,
                   (unsigned long)uai_nor_reset_sr,
                   (unsigned long)uai_nor_reset_cr,
                   (unsigned long)XSPIM->CR);
    cubemx_debug_puts(message);
    if (init_status != BSP_ERROR_NONE) {
        return -1;
    }

    uint8_t model_probe[16] = {0U};
    const int32_t read_status = BSP_XSPI_NOR_Read(
        0U, model_probe, 0x00380000U, sizeof(model_probe));
    const uint32_t *probe_words = (const uint32_t *)model_probe;
    (void)snprintf(message, sizeof(message),
                   "boot: nor indirect read=%ld data=%lx,%lx,%lx,%lx sr=%lx\r\n",
                   (long)read_status, (unsigned long)probe_words[0],
                   (unsigned long)probe_words[1], (unsigned long)probe_words[2],
                   (unsigned long)probe_words[3], (unsigned long)XSPI2->SR);
    cubemx_debug_puts(message);
    if (read_status != BSP_ERROR_NONE) {
        return -1;
    }

    const int32_t map_status = BSP_XSPI_NOR_EnableMemoryMappedMode(0U);
    (void)snprintf(message, sizeof(message),
                   "boot: nor bsp map=%ld stage=%ld sr=%lx cr=%lx\r\n",
                   (long)map_status, (long)uai_nor_bsp_stage,
                   (unsigned long)XSPI2->SR, (unsigned long)XSPI2->CR);
    cubemx_debug_puts(message);
    return map_status == BSP_ERROR_NONE ? 0 : -1;
}

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
