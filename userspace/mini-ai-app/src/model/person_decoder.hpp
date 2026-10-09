#pragma once

#include <cstddef>
#include <cstdint>

#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/foundation/error.hpp"
#include "stai.h"

namespace uai::ai::mini {

/* Mirrors od_st_yolox_pp_static_param_t / od_pp_outBuffer_t from ST's
 * vision_models_pp. The library header pulls in arm_math.h, which is kept
 * out of the C++ build; the layouts must stay identical. */
struct YoloxParams {
    std::int32_t nb_classes = 0;
    std::int32_t nb_anchors = 0;
    std::int32_t grid_width_l = 0;
    std::int32_t grid_height_l = 0;
    std::int32_t grid_width_m = 0;
    std::int32_t grid_height_m = 0;
    std::int32_t grid_width_s = 0;
    std::int32_t grid_height_s = 0;
    std::int32_t nb_input_boxes = 0;
    std::int32_t max_boxes_limit = 0;
    float conf_threshold = 0.0F;
    float iou_threshold = 0.0F;
    const float *anchors_l = nullptr;
    const float *anchors_m = nullptr;
    const float *anchors_s = nullptr;
    std::int32_t nb_detect = 0;
    float raw_l_scale = 0.0F;
    float raw_m_scale = 0.0F;
    float raw_s_scale = 0.0F;
    std::int8_t raw_l_zero_point = 0;
    std::int8_t raw_m_zero_point = 0;
    std::int8_t raw_s_zero_point = 0;
};

struct YoloxDetection {
    float x_center = 0.0F;
    float y_center = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float confidence = 0.0F;
    std::int32_t class_index = 0;
};

/* Turns the three raw YOLOX output tensors into person boxes in Pipe1
 * (800x480) coordinates. Decoding runs on the CPU with ST's vision_models_pp
 * post-processing library. */
class PersonDecoder final {
public:
    static constexpr std::uint32_t kInputWidth = 480U;
    static constexpr std::uint32_t kInputHeight = 480U;
    static constexpr std::uint16_t kOutputCount = 3U;
    static constexpr std::size_t kMaxRawDetections = 100U;

    static constexpr std::size_t InputBytes() { return static_cast<std::size_t>(kInputWidth) * kInputHeight * 3U; }

    /* Reads the output quantization from the network info. Call once. */
    common::Error Configure(const stai_network_info &info);

    /* outputs[i] points at output tensor i as reported by the network info
     * (after the CPU cache has been invalidated for that range). */
    common::Error Decode(
        const void *const *outputs,
        std::uint16_t count,
        inference::BoxSet *boxes
    );

private:
    YoloxParams params_{};
    YoloxDetection detections_[kMaxRawDetections]{};
    std::size_t output_order_[kOutputCount]{};
    bool configured_ = false;
};

} // namespace uai::ai::mini
