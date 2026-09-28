#include "model_face_postprocess.h"

#include "fd_blazeface_pp_if.h"
#include "fd_blazeface_anchors_0.h"
#include "fd_blazeface_anchors_1.h"

#define AI_FACE_BOXES_0 (512U)
#define AI_FACE_BOXES_1 (384U)
#define AI_FACE_TOTAL_BOXES (AI_FACE_BOXES_0 + AI_FACE_BOXES_1)
#define AI_FACE_KEYPOINTS (6U)
#define AI_FACE_MAX_DETECTIONS (16)

static fd_blazeface_pp_static_param_t g_params;
static fd_pp_outBuffer_t g_output[AI_FACE_TOTAL_BOXES];
static fd_pp_keyPoints_t g_keypoints[AI_FACE_TOTAL_BOXES][AI_FACE_KEYPOINTS];

int32_t ai_face_postprocess_initialize(float boxe_0_scale,
                                        int32_t boxe_0_zero_point,
                                        float proba_0_scale,
                                        int32_t proba_0_zero_point,
                                        float boxe_1_scale,
                                        int32_t boxe_1_zero_point,
                                        float proba_1_scale,
                                        int32_t proba_1_zero_point)
{
    g_params = (fd_blazeface_pp_static_param_t){
        .nb_classes = 1,
        .nb_keypoints = AI_FACE_KEYPOINTS,
        .nb_detections_0 = AI_FACE_BOXES_0,
        .nb_detections_1 = AI_FACE_BOXES_1,
        .in_size = 128U,
        .max_boxes_limit = AI_FACE_MAX_DETECTIONS,
        .conf_threshold = AI_FACE_CONF_THRESHOLD,
        .iou_threshold = 0.3F,
        .nb_detect = 0,
        .pAnchors_0 = g_Anchors_0,
        .pAnchors_1 = g_Anchors_1,
        .boxe_0_scale = boxe_0_scale,
        .proba_0_scale = proba_0_scale,
        .boxe_1_scale = boxe_1_scale,
        .proba_1_scale = proba_1_scale,
        .boxe_0_zero_point = (uint8_t)boxe_0_zero_point,
        .proba_0_zero_point = (uint8_t)proba_0_zero_point,
        .boxe_1_zero_point = (uint8_t)boxe_1_zero_point,
        .proba_1_zero_point = (uint8_t)proba_1_zero_point,
    };
    for (uint32_t i = 0U; i < AI_FACE_TOTAL_BOXES; ++i) {
        g_output[i].pKeyPoints = g_keypoints[i];
    }
    return fd_blazeface_pp_reset(&g_params);
}

int32_t ai_face_postprocess_run(const void *raw_detections_0,
                                const void *scores_0,
                                const void *raw_detections_1,
                                const void *scores_1,
                                ai_face_detection_t *detections,
                                uint32_t capacity,
                                uint32_t *count)
{
    if (raw_detections_0 == 0 || scores_0 == 0 || raw_detections_1 == 0 ||
        scores_1 == 0 || detections == 0 || count == 0) {
        return -1;
    }
    fd_blazeface_pp_in_t input = {
        .pRawDetections_0 = (void *)raw_detections_0,
        .pRawDetections_1 = (void *)raw_detections_1,
        .pScores_0 = (void *)scores_0,
        .pScores_1 = (void *)scores_1,
    };
    fd_pp_out_t output = {.pOutBuff = g_output, .nb_detect = 0};
    g_params.nb_detect = 0;
    const int32_t status = fd_blazeface_pp_process_int8(
        &input, &output, &g_params);
    if (status != 0) {
        return status;
    }
    uint32_t available = output.nb_detect > 0 ? (uint32_t)output.nb_detect : 0U;
    uint32_t copied = available < capacity ? available : capacity;
    for (uint32_t i = 0U; i < copied; ++i) {
        detections[i].x_center = g_output[i].x_center;
        detections[i].y_center = g_output[i].y_center;
        detections[i].width = g_output[i].width;
        detections[i].height = g_output[i].height;
        detections[i].confidence = g_output[i].conf;
    }
    *count = copied;
    return 0;
}
