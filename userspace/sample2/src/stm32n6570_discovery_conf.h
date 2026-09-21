/* Local DK configuration.
 *
 * The stock DK configuration places the LCD layer at 0x34200000, which is
 * also used by the generated Neural-ART memory pools.  This application keeps the
 * NPU pools unchanged and puts camera/LCD buffers in the mapped PSRAM.
 */
#ifndef STM32N6570_DISCOVERY_CONF_H
#define STM32N6570_DISCOVERY_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"

#define STM32N6570_DK_A01 0
#define STM32N6570_DK_B01 1
#define STM32N6570_DK_C01 2
#define STM32N6570_DK_REV STM32N6570_DK_C01

#define USE_COM_LOG                         1U
#define USE_BSP_COM_FEATURE                 1U
#define USE_FT5336_TS_CTRL                  1U
#define USE_TS_GESTURE                      1U
#define USE_TS_MULTI_TOUCH                  1U
#define TS_TOUCH_NBR                        2U

/* Keep the first 16 MiB of XSPI1 PSRAM free for Neural-ART virtual pools. */
#define LCD_LAYER_0_ADDRESS                 0x91200000U
#define LCD_LAYER_1_ADDRESS                 0x91300000U

#define BSP_BUTTON_USER1_IT_PRIORITY       15U
#define BSP_BUTTON_USER2_IT_PRIORITY       15U
#define BSP_BUTTON_TAMP_IT_PRIORITY        15U
#define BSP_AUDIO_OUT_IT_PRIORITY          14U
#define BSP_AUDIO_IN_IT_PRIORITY           15U
#define BSP_SD_IT_PRIORITY                 14U
#define BSP_SD_RX_IT_PRIORITY              14U
#define BSP_SD_TX_IT_PRIORITY              15U
#define BSP_TS_IT_PRIORITY                 15U

#define BSP_CAMERA_ISP_DEFAULT_WHITE_BALANCE 255U
#define BSP_CAMERA_ISP_DEFAULT_EXPOSURE      128U
#define BSP_CAMERA_ISP_DEFAULT_CONTRAST      130U
#define BSP_CAMERA_ISP_STATISTICS_AREA_HEIGHT 1940U
#define BSP_CAMERA_ISP_STATISTICS_AREA_WIDTH  2592U

#ifdef __cplusplus
}
#endif

#endif /* STM32N6570_DISCOVERY_CONF_H */
