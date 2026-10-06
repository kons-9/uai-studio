#pragma once

#include <stdint.h>

#include "stm32n6xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t __sample_ai_display0_start__[];
extern uint8_t __sample_ai_display1_start__[];
#ifdef __cplusplus
}
#endif

#define USE_COM_LOG                         0U
#define USE_BSP_COM_FEATURE                 0U
#define USE_FT5336_TS_CTRL                  0U
#define USE_TS_GESTURE                      0U
#define USE_TS_MULTI_TOUCH                  0U
#define TS_TOUCH_NBR                        1U
#define BSP_TS_IT_PRIORITY                  15U

#define LCD_LAYER_0_ADDRESS                 ((uintptr_t)__sample_ai_display0_start__)
#define LCD_LAYER_1_ADDRESS                 ((uintptr_t)__sample_ai_display1_start__)

#define DEFAULT_AUDIO_IN_BUFFER_SIZE        2048U

#define BSP_CAMERA_ISP_DEFAULT_WHITE_BALANCE    255U
#define BSP_CAMERA_ISP_DEFAULT_EXPOSURE         128U
#define BSP_CAMERA_ISP_DEFAULT_CONTRAST         130U
#define BSP_CAMERA_ISP_STATISTICS_AREA_HEIGHT   1940
#define BSP_CAMERA_ISP_STATISTICS_AREA_WIDTH    2592
