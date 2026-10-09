#pragma once

#define HAL_MODULE_ENABLED
#define HAL_RNG_MODULE_ENABLED
#define HAL_HASH_MODULE_ENABLED
#define HAL_CRC_MODULE_ENABLED
#define HAL_RTC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_DCMIPP_MODULE_ENABLED
#define HAL_DMA2D_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_XSPI_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_RAMCFG_MODULE_ENABLED
#define HAL_RIF_MODULE_ENABLED
#define HAL_XSPI_MODULE_ENABLED

#define HSE_VALUE 48000000UL
#define HSI_VALUE 64000000UL
#define LSE_VALUE 32768UL
#define LSI_VALUE 32000UL
#define MSI_VALUE 4000000UL
#define VDD_VALUE 3300UL
#define TICK_INT_PRIORITY 15U
#define USE_RTOS 0U

#define USE_HAL_DCMIPP_REGISTER_CALLBACKS 0U
#define USE_HAL_DMA2D_REGISTER_CALLBACKS 0U
#define USE_HAL_I2C_REGISTER_CALLBACKS 0U
#define USE_HAL_LTDC_REGISTER_CALLBACKS 0U
#define USE_HAL_XSPI_REGISTER_CALLBACKS 0U

#include "stm32n6xx_hal_cortex.h"
#include "stm32n6xx_hal_dma.h"
#include "stm32n6xx_hal_rng.h"
#include "stm32n6xx_hal_hash.h"
#include "stm32n6xx_hal_crc.h"
#include "stm32n6xx_hal_rtc.h"
#include "stm32n6xx_hal_tim.h"
#include "stm32n6xx_hal_dcmipp.h"
#include "stm32n6xx_hal_dma2d.h"
#include "stm32n6xx_hal_exti.h"
#include "stm32n6xx_hal_gpio.h"
#include "stm32n6xx_hal_i2c.h"
#include "stm32n6xx_hal_ltdc.h"
#include "stm32n6xx_hal_xspi.h"
#include "stm32n6xx_hal_pwr.h"
#include "stm32n6xx_hal_ramcfg.h"
#include "stm32n6xx_hal_rcc.h"
#include "stm32n6xx_hal_rcc_ex.h"
#include "stm32n6xx_hal_rif.h"
#include "stm32n6xx_hal_xspi.h"

#ifndef USE_FULL_ASSERT
#define assert_param(expr) ((void)0U)
#endif
