#include "postprocess.hpp"

#include <cstddef>
#include <cstdint>

/*
 * The official post-processing implementations are C files and include
 * CMSIS-DSP headers that are not C++-safe with this toolchain.  Keep their
 * public ABI here instead of including those headers from C++.
 */
typedef float float32_t;

typedef struct {
    void *pRaw_detections_L;
    void *pRaw_detections_M;
    void *pRaw_detections_S;
} od_input_t;

typedef struct {
    float32_t x_center;
    float32_t y_center;
    float32_t width;
    float32_t height;
    float32_t conf;
    int32_t class_index;
} od_detection_t;

typedef struct {
    od_detection_t *pOutBuff;
    int32_t nb_detect;
} od_output_t;

typedef struct {
    int32_t nb_classes;
    int32_t nb_anchors;
    int32_t grid_width_L;
    int32_t grid_height_L;
    int32_t grid_width_M;
    int32_t grid_height_M;
    int32_t grid_width_S;
    int32_t grid_height_S;
    int32_t nb_input_boxes;
    int32_t max_boxes_limit;
    float32_t conf_threshold;
    float32_t iou_threshold;
    const float32_t *pAnchors_L;
    const float32_t *pAnchors_M;
    const float32_t *pAnchors_S;
    int32_t nb_detect;
    float32_t raw_l_scale;
    float32_t raw_m_scale;
    float32_t raw_s_scale;
    int8_t raw_l_zero_point;
    int8_t raw_m_zero_point;
    int8_t raw_s_zero_point;
} od_params_t;

extern "C" {
int32_t od_st_yolox_pp_reset(od_params_t *pInput_static_param);
int32_t od_st_yolox_pp_process_int8(od_input_t *pInput,
                                    od_output_t *pOutput,
                                    od_params_t *pInput_static_param);
}

#define AI_OD_POSTPROCESS_ERROR_NO (0)

namespace {

constexpr std::uint32_t kPersonMaxDetections = 100U;

static od_params_t person_params = {};
static od_detection_t person_buffer[kPersonMaxDetections] = {};
static std::size_t person_order[3] = {};

static const float person_anchors_l[6] = {
    30.0f, 30.0f, 4.2f, 15.0f, 13.8f, 42.0f,
};
static const float person_anchors_m[6] = {
    15.0f, 15.0f, 2.1f, 7.5f, 6.9f, 21.0f,
};
static const float person_anchors_s[6] = {
    7.5f, 7.5f, 1.05f, 3.75f, 3.45f, 10.5f,
};

void sort_outputs(std::size_t *order, std::size_t count,
                  const stai_network_info &info)
{
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
    }
    for (std::size_t i = 1; i < count; ++i) {
        const std::size_t value = order[i];
        std::size_t j = i;
        while (j > 0U &&
               info.outputs[order[j - 1U]].size_bytes >
                   info.outputs[value].size_bytes) {
            order[j] = order[j - 1U];
            --j;
        }
        order[j] = value;
    }
}

} // namespace

namespace inference {

bool initialize(Mode mode, const stai_network_info &info)
{
    if (mode != Mode::Person || info.n_outputs != 3U) {
        return false;
    }
    sort_outputs(person_order, 3U, info);
    person_params = {};
    person_params.nb_classes = 1;
    person_params.nb_anchors = 3;
    person_params.grid_width_L = 60;
    person_params.grid_height_L = 60;
    person_params.grid_width_M = 30;
    person_params.grid_height_M = 30;
    person_params.grid_width_S = 15;
    person_params.grid_height_S = 15;
    person_params.max_boxes_limit = 100;
    person_params.conf_threshold = 0.6f;
    person_params.iou_threshold = 0.5f;
    person_params.pAnchors_L = person_anchors_l;
    person_params.pAnchors_M = person_anchors_m;
    person_params.pAnchors_S = person_anchors_s;
    person_params.raw_s_scale = info.outputs[person_order[0]].scale.data[0];
    person_params.raw_s_zero_point = static_cast<int8_t>(
        info.outputs[person_order[0]].zeropoint.data[0]);
    person_params.raw_m_scale = info.outputs[person_order[1]].scale.data[0];
    person_params.raw_m_zero_point = static_cast<int8_t>(
        info.outputs[person_order[1]].zeropoint.data[0]);
    person_params.raw_l_scale = info.outputs[person_order[2]].scale.data[0];
    person_params.raw_l_zero_point = static_cast<int8_t>(
        info.outputs[person_order[2]].zeropoint.data[0]);
    return od_st_yolox_pp_reset(&person_params) ==
           AI_OD_POSTPROCESS_ERROR_NO;
}

bool process_person(const stai_network_info &, stai_ptr *outputs,
                    ObjectDetection *detections, std::uint32_t capacity,
                    std::uint32_t *count)
{
    if (outputs == nullptr || detections == nullptr || count == nullptr) {
        return false;
    }
    od_input_t input = {};
    input.pRaw_detections_S = outputs[person_order[0]];
    input.pRaw_detections_M = outputs[person_order[1]];
    input.pRaw_detections_L = outputs[person_order[2]];
    od_output_t output = {person_buffer, 0};
    if (od_st_yolox_pp_process_int8(&input, &output, &person_params) !=
        AI_OD_POSTPROCESS_ERROR_NO) {
        return false;
    }
    const std::uint32_t copied =
        static_cast<std::uint32_t>(output.nb_detect) < capacity
            ? static_cast<std::uint32_t>(output.nb_detect)
            : capacity;
    for (std::uint32_t i = 0; i < copied; ++i) {
        detections[i].x_center = output.pOutBuff[i].x_center;
        detections[i].y_center = output.pOutBuff[i].y_center;
        detections[i].width = output.pOutBuff[i].width;
        detections[i].height = output.pOutBuff[i].height;
        detections[i].confidence = output.pOutBuff[i].conf;
    }
    *count = copied;
    return true;
}

} // namespace inference
