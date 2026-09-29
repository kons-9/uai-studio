#include "models/person/person_decoder.hpp"

#include <cstddef>
#include <cstdint>

namespace uai::ai::models::person {

namespace {

constexpr std::uint32_t kMaxPostprocessDetections = 100U;

struct OdInput {
    void *raw_l = nullptr;
    void *raw_m = nullptr;
    void *raw_s = nullptr;
};

struct OdDetection {
    float x_center = 0.0F;
    float y_center = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float confidence = 0.0F;
    std::int32_t class_index = 0;
};

struct OdOutput {
    OdDetection *detections = nullptr;
    std::int32_t count = 0;
};

struct OdParams {
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

extern "C" {
std::int32_t od_st_yolox_pp_reset(OdParams *params);
std::int32_t od_st_yolox_pp_process_int8(OdInput *input, OdOutput *output,
                                         OdParams *params);
}

OdParams g_postprocess{};
OdDetection g_postprocess_buffer[kMaxPostprocessDetections]{};
const float g_anchors_l[6] = {30.0F, 30.0F, 4.2F, 15.0F, 13.8F, 42.0F};
const float g_anchors_m[6] = {15.0F, 15.0F, 2.1F, 7.5F, 6.9F, 21.0F};
const float g_anchors_s[6] = {7.5F, 7.5F, 1.05F, 3.75F, 3.45F, 10.5F};

common::Error Invalid(const char *operation)
{
    return {common::ErrorCode::kModel, 0U, operation};
}

void SortOutputs(const ModelOutputSpec &spec, std::size_t *output_order)
{
    for (std::size_t i = 0U; i < 3U; ++i) {
        output_order[i] = i;
    }
    for (std::size_t i = 1U; i < 3U; ++i) {
        const std::size_t value = output_order[i];
        std::size_t j = i;
        while (j > 0U &&
               spec.tensors[output_order[j - 1U]].size_bytes >
                   spec.tensors[value].size_bytes) {
            output_order[j] = output_order[j - 1U];
            --j;
        }
        output_order[j] = value;
    }
}

bool InitializePostprocess(const ModelOutputSpec &spec,
                           std::size_t *output_order)
{
    if (spec.count != 3U) {
        return false;
    }
    SortOutputs(spec, output_order);

    g_postprocess = {};
    g_postprocess.nb_classes = 1;
    g_postprocess.nb_anchors = 3;
    g_postprocess.grid_width_l = 60;
    g_postprocess.grid_height_l = 60;
    g_postprocess.grid_width_m = 30;
    g_postprocess.grid_height_m = 30;
    g_postprocess.grid_width_s = 15;
    g_postprocess.grid_height_s = 15;
    g_postprocess.max_boxes_limit =
        static_cast<std::int32_t>(kMaxPostprocessDetections);
    g_postprocess.conf_threshold = 0.6F;
    g_postprocess.iou_threshold = 0.5F;
    g_postprocess.anchors_l = g_anchors_l;
    g_postprocess.anchors_m = g_anchors_m;
    g_postprocess.anchors_s = g_anchors_s;
    g_postprocess.raw_s_scale = spec.tensors[output_order[0]].scale;
    g_postprocess.raw_s_zero_point = static_cast<std::int8_t>(
        spec.tensors[output_order[0]].zero_point);
    g_postprocess.raw_m_scale = spec.tensors[output_order[1]].scale;
    g_postprocess.raw_m_zero_point = static_cast<std::int8_t>(
        spec.tensors[output_order[1]].zero_point);
    g_postprocess.raw_l_scale = spec.tensors[output_order[2]].scale;
    g_postprocess.raw_l_zero_point = static_cast<std::int8_t>(
        spec.tensors[output_order[2]].zero_point);
    return od_st_yolox_pp_reset(&g_postprocess) == 0;
}

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

void ProjectDetection(const OdDetection &source,
                      const InferenceGeometry &geometry,
                      Detection *destination)
{
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    if (geometry.projection == InputProjection::kLetterboxed) {
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

    destination->x = ClampCoordinate(left, geometry.frame_width);
    destination->y = ClampCoordinate(top, geometry.frame_height);
    destination->width = ClampCoordinate(width, geometry.frame_width);
    destination->height = ClampCoordinate(height, geometry.frame_height);
    destination->confidence = source.confidence;
    destination->class_index = source.class_index;
}

} // namespace

common::Error Decoder::Initialize(const ModelOutputSpec &spec)
{
    initialized_ = false;
    if (!InitializePostprocess(spec, output_order_)) {
        return Invalid("person.decoder.initialize");
    }
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "person.decoder.initialize"};
}

common::Error Decoder::Decode(const InferenceCompletionContext &context,
                              ModelResult *result) const
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "person.decoder.decode"};
    }
    if (result == nullptr || context.outputs.count < 3U ||
        context.geometry.frame_width == 0U ||
        context.geometry.frame_height == 0U ||
        context.geometry.model_height == 0U ||
        context.geometry.content_height == 0U) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.decoder.decode"};
    }

    const TensorView &raw_s = context.outputs.tensors[output_order_[0]];
    const TensorView &raw_m = context.outputs.tensors[output_order_[1]];
    const TensorView &raw_l = context.outputs.tensors[output_order_[2]];
    if (raw_s.data == nullptr || raw_m.data == nullptr || raw_l.data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "person.decoder.outputs"};
    }

    OdInput input{const_cast<void *>(raw_l.data), const_cast<void *>(raw_m.data),
                  const_cast<void *>(raw_s.data)};
    OdOutput output{g_postprocess_buffer, 0};
    /* The postprocessor appends from nb_detect, so reset it for every frame. */
    g_postprocess.nb_detect = 0;
    if (od_st_yolox_pp_process_int8(&input, &output, &g_postprocess) != 0) {
        return Invalid("person.decoder.postprocess");
    }

    const std::uint32_t available = output.count > 0
                                        ? static_cast<std::uint32_t>(output.count)
                                        : 0U;
    result->kind = ModelKind::kPerson;
    result->detections_valid = true;
    result->detection_count = available < kMaxDecodedDetections
                                  ? available
                                  : kMaxDecodedDetections;
    for (std::uint32_t i = 0U; i < result->detection_count; ++i) {
        ProjectDetection(g_postprocess_buffer[i], context.geometry,
                         &result->detections[i]);
    }
    return {common::ErrorCode::kOk, 0U, "person.decoder.decode"};
}

} // namespace uai::ai::models::person
