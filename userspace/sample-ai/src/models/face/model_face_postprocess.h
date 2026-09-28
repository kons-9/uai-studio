#ifndef UAI_AI_MODEL_FACE_POSTPROCESS_H
#define UAI_AI_MODEL_FACE_POSTPROCESS_H

#include <stdint.h>

/* Lower this value to keep weaker face candidates. */
#define AI_FACE_CONF_THRESHOLD (0.35F)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x_center;
    float y_center;
    float width;
    float height;
    float confidence;
} ai_face_detection_t;

int32_t ai_face_postprocess_initialize(float boxe_0_scale,
                                        int32_t boxe_0_zero_point,
                                        float proba_0_scale,
                                        int32_t proba_0_zero_point,
                                        float boxe_1_scale,
                                        int32_t boxe_1_zero_point,
                                        float proba_1_scale,
                                        int32_t proba_1_zero_point);
int32_t ai_face_postprocess_run(const void *raw_detections_0,
                                const void *scores_0,
                                const void *raw_detections_1,
                                const void *scores_1,
                                ai_face_detection_t *detections,
                                uint32_t capacity,
                                uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif
