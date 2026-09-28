#include "driver/npu_driver/debug.h"

/*
 * Keep diagnostics owned by sample-ai instead of relying on symbols added to
 * a locally modified ll_aton runtime. Weak linkage keeps compatibility with
 * older local runtime copies that already export the same counters.
 */
extern "C" {
volatile unsigned int g_aton_irq_count __attribute__((weak)) = 0U;
volatile unsigned int g_aton_last_irqs __attribute__((weak)) = 0U;
volatile unsigned int g_npu_cache_init_status __attribute__((weak)) = 0U;
volatile unsigned int g_npu_cache_enable_status __attribute__((weak)) = 0U;
volatile unsigned int g_npu_cache_invalidate_status __attribute__((weak)) = 0U;
volatile unsigned int g_npu_cache_cr1 __attribute__((weak)) = 0U;
volatile unsigned int g_npu_cache_sr __attribute__((weak)) = 0U;
}
