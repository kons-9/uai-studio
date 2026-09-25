#ifndef UAI_AI_MODEL_SEGMENTATION_DIAGNOSTICS_H
#define UAI_AI_MODEL_SEGMENTATION_DIAGNOSTICS_H

#include <stdint.h>

#include "stai.h"

#ifdef __cplusplus
extern "C" {
#endif

int ai_segmentation_diag_begin(void);
void ai_segmentation_diag_dump(void);
void ai_segmentation_diag_poll(uint32_t tick, uint32_t stai_status);
void ai_segmentation_diag_irq(uint32_t edge);
void ai_segmentation_diag_epoch_callback(void *cookie,
                                         stai_event_type event,
                                         const void *payload);

#ifdef __cplusplus
}
#endif

#endif
