#include "model/person_decoder.hpp"

#include "middleware/pipeline/image_format.hpp"

extern "C" {
std::int32_t od_st_yolox_pp_reset(uai::ai::mini::YoloxParams *params);
std::int32_t od_st_yolox_pp_process_int8(
    void *input,
    void *output,
    uai::ai::mini::YoloxParams *params
);
}

namespace uai::ai::mini {

namespace {

/* Anchors of st_yolo_x_nano_480 for the 60x60, 30x30, and 15x15 grids. */
constexpr float kAnchorsL[6] = {30.0F, 30.0F, 4.2F, 15.0F, 13.8F, 42.0F};
constexpr float kAnchorsM[6] = {15.0F, 15.0F, 2.1F, 7.5F, 6.9F, 21.0F};
constexpr float kAnchorsS[6] = {7.5F, 7.5F, 1.05F, 3.75F, 3.45F, 10.5F};

struct YoloxInput {
    void *raw_l = nullptr;
    void *raw_m = nullptr;
    void *raw_s = nullptr;
};

struct YoloxOutput {
    YoloxDetection *detections = nullptr;
    std::int32_t count = 0;
};

/* The network reports its outputs in generation order; the library wants
 * them by grid size. Sort indices by tensor size: S < M < L. */
void SortOutputsBySize(
    const stai_network_info &info,
    std::size_t *order
)
{
    for (std::size_t i = 0U; i < PersonDecoder::kOutputCount; ++i)
        order[i] = i;
    for (std::size_t i = 1U; i < PersonDecoder::kOutputCount; ++i) {
        const std::size_t value = order[i];
        std::size_t j = i;
        while (j > 0U && info.outputs[order[j - 1U]].size_bytes > info.outputs[value].size_bytes) {
            order[j] = order[j - 1U];
            --j;
        }
        order[j] = value;
    }
}

std::int16_t ClampTo(
    float value,
    std::uint32_t limit
)
{
    if (value <= 0.0F)
        return 0;
    if (value >= static_cast<float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

/* Pipe2 letterboxes the 800x480 image into 480x480: the content is 480x288
 * centered vertically. Map normalized model coordinates back to Pipe1. */
inference::Box ProjectToCapture(const YoloxDetection &detection)
{
    constexpr float kFrameWidth = pipeline::kCaptureFormat.width;
    constexpr float kFrameHeight = pipeline::kCaptureFormat.height;
    constexpr float kModelHeight = PersonDecoder::kInputHeight;
    constexpr float kContentHeight = pipeline::kInferenceContentFormat.height;
    constexpr float kPadTop = (kModelHeight - kContentHeight) / 2.0F;
    constexpr float kVerticalScale = kFrameHeight / kContentHeight;

    const float left = (detection.x_center - detection.width * 0.5F) * kFrameWidth;
    const float top = ((detection.y_center - detection.height * 0.5F) * kModelHeight - kPadTop) * kVerticalScale;
    const float width = detection.width * kFrameWidth;
    const float height = detection.height * kModelHeight * kVerticalScale;

    inference::Box box{};
    box.x = ClampTo(left, pipeline::kCaptureFormat.width);
    box.y = ClampTo(top, pipeline::kCaptureFormat.height);
    box.width = ClampTo(width, pipeline::kCaptureFormat.width);
    box.height = ClampTo(height, pipeline::kCaptureFormat.height);
    box.confidence = detection.confidence;
    return box;
}

} // namespace

common::Error PersonDecoder::Configure(const stai_network_info &info)
{
    configured_ = false;
    if (info.outputs == nullptr || info.n_outputs != kOutputCount) {
        return {common::ErrorCode::kModel};
    }
    SortOutputsBySize(info, output_order_);

    params_ = {};
    params_.nb_classes = 1;
    params_.nb_anchors = 3;
    params_.grid_width_l = 60;
    params_.grid_height_l = 60;
    params_.grid_width_m = 30;
    params_.grid_height_m = 30;
    params_.grid_width_s = 15;
    params_.grid_height_s = 15;
    params_.max_boxes_limit = static_cast<std::int32_t>(kMaxRawDetections);
    params_.conf_threshold = 0.6F;
    params_.iou_threshold = 0.5F;
    params_.anchors_l = kAnchorsL;
    params_.anchors_m = kAnchorsM;
    params_.anchors_s = kAnchorsS;
    const auto &small = info.outputs[output_order_[0]];
    const auto &medium = info.outputs[output_order_[1]];
    const auto &large = info.outputs[output_order_[2]];
    params_.raw_s_scale = small.scale.data[0];
    params_.raw_s_zero_point = static_cast<std::int8_t>(small.zeropoint.data[0]);
    params_.raw_m_scale = medium.scale.data[0];
    params_.raw_m_zero_point = static_cast<std::int8_t>(medium.zeropoint.data[0]);
    params_.raw_l_scale = large.scale.data[0];
    params_.raw_l_zero_point = static_cast<std::int8_t>(large.zeropoint.data[0]);
    if (od_st_yolox_pp_reset(&params_) != 0) {
        return {common::ErrorCode::kModel};
    }
    configured_ = true;
    return {};
}

common::Error PersonDecoder::Decode(
    const void *const *outputs,
    std::uint16_t count,
    inference::BoxSet *boxes
)
{
    if (!configured_)
        return {common::ErrorCode::kNotInitialized};
    if (boxes == nullptr || outputs == nullptr || count < kOutputCount) {
        return {common::ErrorCode::kInvalidArgument};
    }
    YoloxInput input{
        const_cast<void *>(outputs[output_order_[2]]),
        const_cast<void *>(outputs[output_order_[1]]),
        const_cast<void *>(outputs[output_order_[0]])
    };
    if (input.raw_l == nullptr || input.raw_m == nullptr || input.raw_s == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    YoloxOutput output{detections_, 0};
    params_.nb_detect = 0;
    if (od_st_yolox_pp_process_int8(&input, &output, &params_) != 0) {
        return {common::ErrorCode::kModel};
    }

    boxes->person = {};
    const std::uint32_t available = output.count > 0 ? static_cast<std::uint32_t>(output.count) : 0U;
    boxes->person.count =
        available < inference::kMaxBoxes ? available : static_cast<std::uint32_t>(inference::kMaxBoxes);
    for (std::uint32_t i = 0U; i < boxes->person.count; ++i) {
        boxes->person.boxes[i] = ProjectToCapture(detections_[i]);
    }
    boxes->person_valid = true;
    return {};
}

} // namespace uai::ai::mini
