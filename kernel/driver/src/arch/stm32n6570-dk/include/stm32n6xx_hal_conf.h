#ifndef STM32N6xx_HAL_CONF_H
#define STM32N6xx_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_DCMIPP_MODULE_ENABLED
#define HAL_DMA2D_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_HCD_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_RAMCFG_MODULE_ENABLED
#define HAL_MDF_MODULE_ENABLED
#define HAL_RIF_MODULE_ENABLED
#define HAL_SAI_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_XSPI_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED

#define HSE_VALUE              24000000UL
#define HSE_STARTUP_TIMEOUT    100UL
#define HSI_VALUE              64000000UL
#define LSE_VALUE              32768UL
#define LSE_STARTUP_TIMEOUT    5000UL
#define MSI_VALUE              4000000UL
#define VDD_VALUE              3300UL
#define TICK_INT_PRIORITY      15U
#define USE_RTOS               0U
#define USE_SPI_CRC             0U

#define USE_HAL_DCMIPP_REGISTER_CALLBACKS  0U
#define USE_HAL_DMA2D_REGISTER_CALLBACKS   0U
#define USE_HAL_I2C_REGISTER_CALLBACKS     0U
#define USE_HAL_LTDC_REGISTER_CALLBACKS    0U

#define assert_param(expr) ((void)0U)

#include "stm32n6xx_hal_rcc.h"
#include "stm32n6xx_hal_gpio.h"
#include "stm32n6xx_hal_exti.h"
#include "stm32n6xx_hal_rif.h"
#include "stm32n6xx_hal_dma.h"
#include "stm32n6xx_hal_cortex.h"
#include "stm32n6xx_hal_dcmipp.h"
#include "stm32n6xx_hal_dma2d.h"
#include "stm32n6xx_hal_hcd.h"
#include "stm32n6xx_hal_i2c.h"
#include "stm32n6xx_hal_ltdc.h"
#include "stm32n6xx_hal_ramcfg.h"
#include "stm32n6xx_hal_mdf.h"
#include "stm32n6xx_hal_pwr.h"
#include "stm32n6xx_hal_sai.h"
#include "stm32n6xx_hal_uart.h"
#include "stm32n6xx_hal_xspi.h"

#endif
