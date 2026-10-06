#pragma once

#include "stm32n6xx_hal.h"

#define USE_COM_LOG                         0U
#define USE_BSP_COM_FEATURE                 0U

#define USE_FT5336_TS_CTRL                  0U
#define USE_TS_GESTURE                      0U
#define USE_TS_MULTI_TOUCH                  0U
#define TS_TOUCH_NBR                        1U
#define BSP_TS_IT_PRIORITY                  15U

/* The LCD scans one of two composed RGB565 frames separate from capture. */
#define LCD_LAYER_0_ADDRESS                 0x34030000U
#define LCD_LAYER_1_ADDRESS                 0x34100000U

#define DEFAULT_AUDIO_IN_BUFFER_SIZE        2048U

#define BSP_CAMERA_ISP_DEFAULT_WHITE_BALANCE    255U
#define BSP_CAMERA_ISP_DEFAULT_EXPOSURE         128U
#define BSP_CAMERA_ISP_DEFAULT_CONTRAST        130U
#define BSP_CAMERA_ISP_STATISTICS_AREA_HEIGHT  1940
#define BSP_CAMERA_ISP_STATISTICS_AREA_WIDTH   2592
