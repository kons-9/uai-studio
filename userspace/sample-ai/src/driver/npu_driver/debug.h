#ifndef UAI_AI_NPU_DRIVER_DEBUG_H
#define UAI_AI_NPU_DRIVER_DEBUG_H

/* These diagnostics are implemented by the linked NPU/ATON platform code.
 * This header declares the shared counters for ai diagnostics. */
#ifdef __cplusplus
extern "C" {
#endif

extern volatile unsigned int g_aton_irq_count;
extern volatile unsigned int g_aton_last_irqs;
extern volatile unsigned int g_npu_init_stage;
extern volatile unsigned int g_npu_cache_init_status;
extern volatile unsigned int g_npu_cache_enable_status;
extern volatile unsigned int g_npu_cache_invalidate_status;
extern volatile unsigned int g_npu_cache_cr1;
extern volatile unsigned int g_npu_cache_sr;

#ifdef __cplusplus
}
#endif

#endif
