#ifndef UAI_AI_C_BSP_XSPI_BSP_H
#define UAI_AI_C_BSP_XSPI_BSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* STM32N6570-DK BSP API implemented by the adjacent C source. */
#include "stm32n6570_discovery_xspi.h"

/* Project diagnostics defined by stm32n6570_discovery_xspi.c. */
extern volatile int32_t uai_nor_bsp_stage;
extern volatile int32_t uai_nor_reset_stage;
extern volatile int32_t uai_nor_reset_result;
extern volatile uint32_t uai_nor_reset_hal_error;
extern volatile uint32_t uai_nor_reset_sr;
extern volatile uint32_t uai_nor_reset_cr;
extern volatile uint32_t uai_nor_sr_after_init;
extern volatile uint32_t uai_nor_cr_after_init;

#ifdef __cplusplus
}
#endif

#endif /* UAI_AI_C_BSP_XSPI_BSP_H */
