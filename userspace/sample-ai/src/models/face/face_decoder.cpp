#include "models/face/face_decoder.hpp"

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "arm_math.h"
#include "fd_blazeface_anchors_0.h"
#include "fd_blazeface_anchors_1.h"
#include "fd_blazeface_pp_if.h"

namespace uai::ai::models::face {

namespace {

using common::Error;
using common::ErrorCode;

constexpr std::size_t kBoxes0 = 512U;
constexpr std::size_t kBoxes1 = 384U;
constexpr std::size_t kTotalBoxes = kBoxes0 + kBoxes1;
constexpr std::size_t kKeypoints = 6U;

struct RawDetection {
    float x_center = 0.0F;
    float y_center = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float confidence = 0.0F;
};

fd_blazeface_pp_static_param_t g_face_params{};
fd_pp_outBuffer_t g_face_output[kTotalBoxes]{};
fd_pp_keyPoints_t g_face_keypoints[kTotalBoxes][kKeypoints]{};

float ClampCoordinate(float value, std::uint32_t limit)
{
    if (value <= 0.0F) {
        return 0.0F;
    }
    if (value >= static_cast<float>(limit)) {
        return static_cast<float>(limit);
    }
    return value;
}

Error Invalid(const char *operation)
{
    return {ErrorCode::kModel, 0U, operation};
}

bool InitializeFacePostprocess(const ModelOutputSpec &spec,
                               const std::size_t *output_order)
{
    const std::size_t box0 = output_order[0];
    const std::size_t score0 = output_order[1];
    const std::size_t score1 = output_order[2];
    const std::size_t box1 = output_order[3];

    g_face_params = {};
    g_face_params.nb_classes = 1;
    g_face_params.nb_keypoints = kKeypoints;
    g_face_params.nb_detections_0 = kBoxes0;
    g_face_params.nb_detections_1 = kBoxes1;
    g_face_params.in_size = 128U;
    g_face_params.max_boxes_limit = kMaxDecodedDetections;
    g_face_params.conf_threshold = 0.35F;
    g_face_params.iou_threshold = 0.3F;
    g_face_params.pAnchors_0 = g_Anchors_0;
    g_face_params.pAnchors_1 = g_Anchors_1;
    g_face_params.boxe_0_scale = spec.tensors[box0].scale;
    g_face_params.boxe_0_zero_point = static_cast<std::uint8_t>(
        spec.tensors[box0].zero_point);
    g_face_params.proba_0_scale = spec.tensors[score0].scale;
    g_face_params.proba_0_zero_point = static_cast<std::uint8_t>(
        spec.tensors[score0].zero_point);
    g_face_params.boxe_1_scale = spec.tensors[box1].scale;
    g_face_params.boxe_1_zero_point = static_cast<std::uint8_t>(
        spec.tensors[box1].zero_point);
    g_face_params.proba_1_scale = spec.tensors[score1].scale;
    g_face_params.proba_1_zero_point = static_cast<std::uint8_t>(
        spec.tensors[score1].zero_point);
    for (std::size_t i = 0U; i < kTotalBoxes; ++i) {
        g_face_output[i].pKeyPoints = g_face_keypoints[i];
    }
    return fd_blazeface_pp_reset(&g_face_params) == 0;
}

bool RunFacePostprocess(const void *raw_detections_0,
                        const void *scores_0,
                        const void *raw_detections_1,
                        const void *scores_1,
                        RawDetection *detections,
                        std::uint32_t capacity,
                        std::uint32_t *count)
{
    if (raw_detections_0 == nullptr || scores_0 == nullptr ||
        raw_detections_1 == nullptr || scores_1 == nullptr ||
        detections == nullptr || count == nullptr) {
        return false;
    }

    fd_blazeface_pp_in_t input{
        const_cast<void *>(raw_detections_0),
        const_cast<void *>(raw_detections_1),
        const_cast<void *>(scores_0),
        const_cast<void *>(scores_1)};
    fd_pp_out_t output{g_face_output, 0};
    g_face_params.nb_detect = 0;
    if (fd_blazeface_pp_process_int8(&input, &output, &g_face_params) != 0) {
        return false;
    }

    const std::uint32_t available = output.nb_detect > 0
                                        ? static_cast<std::uint32_t>(
                                              output.nb_detect)
                                        : 0U;
    const std::uint32_t copied = available < capacity ? available : capacity;
    for (std::uint32_t i = 0U; i < copied; ++i) {
        detections[i].x_center = g_face_output[i].x_center;
        detections[i].y_center = g_face_output[i].y_center;
        detections[i].width = g_face_output[i].width;
        detections[i].height = g_face_output[i].height;
        detections[i].confidence = g_face_output[i].conf;
    }
    *count = copied;
    return true;
}

} // namespace

Error Decoder::Initialize(const ModelOutputSpec &spec)
{
    initialized_ = false;
    if (spec.count != 4U) {
        return Invalid("face.decoder.output_count");
    }

    bool found_box0 = false;
    bool found_score0 = false;
    bool found_score1 = false;
    bool found_box1 = false;
    for (std::size_t i = 0U; i < spec.count; ++i) {
        switch (spec.tensors[i].size_bytes) {
        case kBoxes0 * 16U:
            output_order_[0] = i;
            found_box0 = true;
            break;
        case kBoxes0:
            output_order_[1] = i;
            found_score0 = true;
            break;
        case kBoxes1:
            output_order_[2] = i;
            found_score1 = true;
            break;
        case kBoxes1 * 16U:
            output_order_[3] = i;
            found_box1 = true;
            break;
        default:
            return Invalid("face.decoder.output_shape");
        }
    }
    if (!found_box0 || !found_score0 || !found_score1 || !found_box1) {
        return Invalid("face.decoder.output_layout");
    }

    if (!InitializeFacePostprocess(spec, output_order_)) {
        return Invalid("face.decoder.initialize");
    }

    initialized_ = true;
    return {ErrorCode::kOk, 0U, "face.decoder.initialize"};
}

Error Decoder::Decode(const InferenceCompletionContext &context,
                      ModelResult *result) const
{
    if (!initialized_) {
        return {ErrorCode::kNotInitialized, 0U, "face.decoder.decode"};
    }
    if (result == nullptr || context.outputs.count < 4U ||
        context.geometry.frame_width == 0U ||
        context.geometry.frame_height == 0U) {
        return {ErrorCode::kInvalidArgument, 0U, "face.decoder.decode"};
    }

    const TensorView &box0 = context.outputs.tensors[output_order_[0]];
    const TensorView &score0 = context.outputs.tensors[output_order_[1]];
    const TensorView &score1 = context.outputs.tensors[output_order_[2]];
    const TensorView &box1 = context.outputs.tensors[output_order_[3]];
    if (box0.data == nullptr || score0.data == nullptr ||
        score1.data == nullptr || box1.data == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U, "face.decoder.outputs"};
    }

    RawDetection raw[kMaxDecodedDetections]{};
    std::uint32_t raw_count = 0U;
    if (!RunFacePostprocess(box0.data, score0.data, box1.data, score1.data,
                            raw, kMaxDecodedDetections, &raw_count)) {
        return Invalid("face.decoder.postprocess");
    }

    result->kind = ModelKind::kFace;
    result->detections_valid = true;
    result->detection_count = raw_count < kMaxDecodedDetections
                                  ? raw_count
                                  : kMaxDecodedDetections;
    const InferenceGeometry &geometry = context.geometry;
    for (std::uint32_t i = 0U; i < result->detection_count; ++i) {
        const RawDetection &source = raw[i];
        float left = 0.0F;
        float top = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        if (geometry.projection == InputProjection::kLetterboxed) {
            if (geometry.content_height == 0U ||
                geometry.model_height == 0U) {
                return Invalid("face.decoder.geometry");
            }
            left = (source.x_center - source.width * 0.5F) *
                   static_cast<float>(geometry.frame_width);
            top = ((source.y_center - source.height * 0.5F) *
                       static_cast<float>(geometry.model_height) -
                   static_cast<float>(geometry.pad_top)) *
                  static_cast<float>(geometry.frame_height) /
                  static_cast<float>(geometry.content_height);
            width = source.width * static_cast<float>(geometry.frame_width);
            height = source.height * static_cast<float>(geometry.model_height) *
                     static_cast<float>(geometry.frame_height) /
                     static_cast<float>(geometry.content_height);
        } else {
            const float crop_size = static_cast<float>(geometry.frame_height);
            const float crop_x =
                (static_cast<float>(geometry.frame_width) - crop_size) * 0.5F;
            left = crop_x +
                   (source.x_center - source.width * 0.5F) * crop_size;
            top = (source.y_center - source.height * 0.5F) * crop_size;
            width = source.width * crop_size;
            height = source.height * crop_size;
        }
        result->detections[i].x =
            ClampCoordinate(left, geometry.frame_width);
        result->detections[i].y =
            ClampCoordinate(top, geometry.frame_height);
        result->detections[i].width =
            ClampCoordinate(width, geometry.frame_width);
        result->detections[i].height =
            ClampCoordinate(height, geometry.frame_height);
        result->detections[i].confidence = source.confidence;
    }
    return {ErrorCode::kOk, 0U, "face.decoder.decode"};
}

} // namespace uai::ai::models::face
