#ifndef UAI_AI_MODEL_SEGMENTATION_C_API_H
#define UAI_AI_MODEL_SEGMENTATION_C_API_H

#include "model_manager/model_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const ai_model_c_api segmentation_model_c_api;

#if defined(AI_SEGMENTATION_DIAG)
int ai_segmentation_diag_begin(void);
void ai_segmentation_diag_dump(void);
void ai_segmentation_diag_poll(uint32_t tick, uint32_t stai_status);
void ai_segmentation_diag_irq(uint32_t edge);
void ai_segmentation_diag_epoch_callback(void *cookie,
                                         stai_event_type event,
                                         const void *payload);
#endif

#ifdef __cplusplus
}
#endif

#endif
