/*
 * sample2-local BSP configuration.
 * Only the external-memory XSPI drivers are linked by sample2; display,
 * camera and audio configuration is intentionally not pulled into this
 * application.
 */
#ifndef STM32N6570_DISCOVERY_CONF_H
#define STM32N6570_DISCOVERY_CONF_H

#include "stm32n6xx_hal.h"

#define USE_COM_LOG                 0U
#define USE_BSP_COM_FEATURE         0U
#define USE_FT5336_TS_CTRL          0U
#define USE_TS_GESTURE              0U
#define USE_TS_MULTI_TOUCH          0U
#define TS_TOUCH_NBR                0U
#define USE_AUDIO_CODEC_WM8904

#define BSP_SDRAM_IT_PRIORITY       15U
#define BSP_BUTTON_USER1_IT_PRIORITY 15U
#define BSP_BUTTON_USER2_IT_PRIORITY 15U
#define BSP_BUTTON_TAMP_IT_PRIORITY  15U
#define BSP_AUDIO_OUT_IT_PRIORITY   14U
#define BSP_AUDIO_IN_IT_PRIORITY    15U
#define BSP_SD_IT_PRIORITY          14U
#define BSP_SD_RX_IT_PRIORITY       14U
#define BSP_SD_TX_IT_PRIORITY       15U
#define BSP_TS_IT_PRIORITY          15U

#endif
