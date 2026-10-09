#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* STM32N6570-DK BSP API implemented by the adjacent C source. */
#include "stm32n6570_discovery_xspi.h"

/* Register-based NOR failure classifications. These identify the XSPI-side
 * failure signature; they do not prove the external NOR's protocol state. */
typedef enum {
    UAI_NOR_DIAG_NONE = 0,
    UAI_NOR_DIAG_TIMEOUT_BUSY_NO_EVENT = 1,
    UAI_NOR_DIAG_TIMEOUT_BUSY_WITH_EVENT = 2,
    UAI_NOR_DIAG_TIMEOUT_NOT_BUSY = 3,
    UAI_NOR_DIAG_HAL_ERROR = 4,
    UAI_NOR_DIAG_COMPONENT_FAILURE_NO_HAL_ERROR = 5
} UAI_NOR_DiagnosticCode_t;

typedef enum {
    UAI_NOR_DIAG_STAGE_XSPI_INIT = 2,
    UAI_NOR_DIAG_STAGE_RESET_ENABLE_OPI_DTR = 10,
    UAI_NOR_DIAG_STAGE_RESET_OPI_DTR = 11,
    UAI_NOR_DIAG_STAGE_MEM_READY = 12,
    UAI_NOR_DIAG_STAGE_FLASH_CONFIG = 13,
    UAI_NOR_DIAG_STAGE_CLOCK_CONFIG = 20,
    UAI_NOR_DIAG_STAGE_XSPIM_CONFIG = 21,
    UAI_NOR_DIAG_STAGE_STR_READ = 30,
    UAI_NOR_DIAG_STAGE_DTR_READ = 31
} UAI_NOR_DiagnosticStage_t;

/* Project diagnostics defined by stm32n6570_discovery_xspi.c. */
extern volatile int32_t uai_nor_bsp_stage;
extern volatile int32_t uai_nor_reset_stage;
extern volatile int32_t uai_nor_reset_result;
extern volatile uint32_t uai_nor_reset_hal_error;
extern volatile uint32_t uai_nor_reset_sr;
extern volatile uint32_t uai_nor_reset_cr;
extern volatile uint32_t uai_nor_sr_after_init;
extern volatile uint32_t uai_nor_cr_after_init;
extern volatile uint32_t uai_nor_diag_code;
extern volatile uint32_t uai_nor_diag_stage;
extern volatile int32_t uai_nor_diag_component_result;
extern volatile uint32_t uai_nor_diag_hal_error;
extern volatile uint32_t uai_nor_diag_hal_state;
extern volatile uint32_t uai_nor_diag_sr;
extern volatile uint32_t uai_nor_diag_cr;
extern volatile uint32_t uai_nor_diag_ccr;
extern volatile uint32_t uai_nor_diag_dlr;
extern volatile uint32_t uai_nor_diag_ir;
extern volatile uint32_t uai_nor_diag_ar;
extern volatile uint32_t uai_nor_diag_xspim_cr;

#ifdef __cplusplus
}
#endif
