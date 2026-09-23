#ifndef SAMPLE2_STM32N6XX_HAL_CONF_H
#define SAMPLE2_STM32N6XX_HAL_CONF_H

/* CubeMX owns the common HAL selection. sample2 also initializes the NPU RAM
 * and CACHEAXI from its memory manager. */
#define HAL_CACHEAXI_MODULE_ENABLED
#define HAL_RAMCFG_MODULE_ENABLED

#include_next "stm32n6xx_hal_conf.h"

#include "stm32n6xx_hal_cacheaxi.h"

#endif /* SAMPLE2_STM32N6XX_HAL_CONF_H */
