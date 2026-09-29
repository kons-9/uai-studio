#ifndef UAI_AI_NPU_DRIVER_DEBUG_H
#define UAI_AI_NPU_DRIVER_DEBUG_H

/* Diagnostics are owned by sample-ai. Weak definitions keep compatibility
 * with older local ATON/cache copies that already export these counters. */
#ifdef __cplusplus
extern "C" {
#endif

extern volatile unsigned int g_aton_irq_count;
extern volatile unsigned int g_aton_last_irqs;
extern volatile unsigned int g_npu_cache_init_status;
extern volatile unsigned int g_npu_cache_enable_status;
extern volatile unsigned int g_npu_cache_invalidate_status;
extern volatile unsigned int g_npu_cache_cr1;
extern volatile unsigned int g_npu_cache_sr;

#ifdef __cplusplus
}
#endif

#endif
