#ifndef UAI_TEST_STM32N6XX_HAL_H
#define UAI_TEST_STM32N6XX_HAL_H

#include <stdint.h>

typedef struct {
    volatile uint32_t CYCCNT;
    volatile uint32_t CTRL;
} UaiTestDwt;

typedef struct {
    volatile uint32_t DEMCR;
} UaiTestCoreDebug;

extern UaiTestDwt uai_test_dwt;
extern UaiTestCoreDebug uai_test_core_debug;

#define DWT (&uai_test_dwt)
#define CoreDebug (&uai_test_core_debug)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define CoreDebug_DEMCR_TRCENA_Msk 1U

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
void SCB_CleanDCache_by_Addr(uint32_t *address, int32_t size);

#endif