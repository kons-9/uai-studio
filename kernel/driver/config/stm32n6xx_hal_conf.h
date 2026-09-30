#ifndef AI_STM32N6XX_HAL_CONF_H
#define AI_STM32N6XX_HAL_CONF_H

/* CubeMX owns the common HAL selection. experiment-ai also initializes the NPU RAM
 * and CACHEAXI from its memory manager. */
#define HAL_CACHEAXI_MODULE_ENABLED
#define HAL_RAMCFG_MODULE_ENABLED
#define HAL_DCMIPP_MODULE_ENABLED
#define HAL_DMA2D_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_RIF_MODULE_ENABLED
#define HAL_XSPI_MODULE_ENABLED

#include_next "stm32n6xx_hal_conf.h"

#include "stm32n6xx_hal_cacheaxi.h"

#endif /* AI_STM32N6XX_HAL_CONF_H */
