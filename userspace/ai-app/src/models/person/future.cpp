#include "models/person/future.hpp"

#include <cstdint>

#include "middleware/foundation/log.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/npu_network.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "middleware/memory/buffer_types.hpp"
#include "middleware/memory/generated/memory_config.hpp"

namespace uai::ai::models::person {

namespace {

constexpr std::uint32_t kMaxPostprocessDetections = 100U;
constexpr std::size_t kMaxModelOutputs =
    memory_manager::kMemoryConfig.model_output_bytes.size();
constexpr std::size_t kMaxDecodedDetections = inference::kMaxBoxes;

struct ModelOutputView {
    const void *tensors[kMaxModelOutputs]{};
    std::uint16_t count = 0U;
};

enum class InputProjection : std::uint8_t {
    kLetterboxed,
    kCenteredSquare,
};

struct InferenceGeometry {
    InputProjection projection = InputProjection::kLetterboxed;
    std::uint32_t frame_width = 0U;
    std::uint32_t frame_height = 0U;
    std::uint32_t model_width = 0U;
    std::uint32_t model_height = 0U;
    std::uint32_t content_height = 0U;
    std::uint32_t pad_top = 0U;
};

struct Detection {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float confidence = 0.0F;
    std::int32_t class_index = 0;
};

struct ModelResult {
    bool detections_valid = false;
    std::uint32_t detection_count = 0U;
    Detection detections[kMaxDecodedDetections]{};
};

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
std::size_t g_output_order[3]{};
bool g_decoder_initialized = false;
const float g_anchors_l[6] = {30.0F, 30.0F, 4.2F, 15.0F, 13.8F, 42.0F};
const float g_anchors_m[6] = {15.0F, 15.0F, 2.1F, 7.5F, 6.9F, 21.0F};
const float g_anchors_s[6] = {7.5F, 7.5F, 1.05F, 3.75F, 3.45F, 10.5F};

void SortOutputs(const stai_network_info &info, std::size_t *output_order)
{
    for (std::size_t i = 0U; i < 3U; ++i) output_order[i] = i;
    for (std::size_t i = 1U; i < 3U; ++i) {
        const std::size_t value = output_order[i];
        std::size_t j = i;
        while (j > 0U &&
               info.outputs[output_order[j - 1U]].size_bytes >
                   info.outputs[value].size_bytes) {
            output_order[j] = output_order[j - 1U];
            --j;
        }
        output_order[j] = value;
    }
}

common::Error InitializeDecoder(const stai_network_info &info)
{
    g_decoder_initialized = false;
    if (info.outputs == nullptr || info.n_outputs != 3U ||
        info.n_outputs > kMaxModelOutputs) {
        return common::Error{common::ErrorCode::kModel};
    }
    SortOutputs(info, g_output_order);

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
    g_postprocess.raw_s_scale =
        info.outputs[g_output_order[0]].scale.data[0];
    g_postprocess.raw_s_zero_point = static_cast<std::int8_t>(
        info.outputs[g_output_order[0]].zeropoint.data[0]);
    g_postprocess.raw_m_scale =
        info.outputs[g_output_order[1]].scale.data[0];
    g_postprocess.raw_m_zero_point = static_cast<std::int8_t>(
        info.outputs[g_output_order[1]].zeropoint.data[0]);
    g_postprocess.raw_l_scale =
        info.outputs[g_output_order[2]].scale.data[0];
    g_postprocess.raw_l_zero_point = static_cast<std::int8_t>(
        info.outputs[g_output_order[2]].zeropoint.data[0]);
    if (od_st_yolox_pp_reset(&g_postprocess) != 0) {
        return common::Error{common::ErrorCode::kModel};
    }
    g_decoder_initialized = true;
    return {common::ErrorCode::kOk};
}

float ClampProjectedCoordinate(float value, std::uint32_t limit)
{
    if (value <= 0.0F) return 0.0F;
    if (value >= static_cast<float>(limit)) return static_cast<float>(limit);
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

    destination->x = ClampProjectedCoordinate(left, geometry.frame_width);
    destination->y = ClampProjectedCoordinate(top, geometry.frame_height);
    destination->width =
        ClampProjectedCoordinate(width, geometry.frame_width);
    destination->height =
        ClampProjectedCoordinate(height, geometry.frame_height);
    destination->confidence = source.confidence;
    destination->class_index = source.class_index;
}

common::Error DecodePerson(const ModelOutputView &outputs,
                           const InferenceGeometry &geometry,
                           ModelResult *result)
{
    if (!g_decoder_initialized) {
        return {common::ErrorCode::kNotInitialized};
    }
    if (result == nullptr || outputs.count < 3U ||
        geometry.frame_width == 0U || geometry.frame_height == 0U ||
        geometry.model_height == 0U || geometry.content_height == 0U) {
        return {common::ErrorCode::kInvalidArgument};
    }

    const void *raw_s = outputs.tensors[g_output_order[0]];
    const void *raw_m = outputs.tensors[g_output_order[1]];
    const void *raw_l = outputs.tensors[g_output_order[2]];
    if (raw_s == nullptr || raw_m == nullptr || raw_l == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }

    OdInput input{const_cast<void *>(raw_l), const_cast<void *>(raw_m),
                  const_cast<void *>(raw_s)};
    OdOutput output{g_postprocess_buffer, 0};
    g_postprocess.nb_detect = 0;
    if (od_st_yolox_pp_process_int8(&input, &output, &g_postprocess) != 0) {
        return common::Error{common::ErrorCode::kModel};
    }

    const std::uint32_t available = output.count > 0
                                        ? static_cast<std::uint32_t>(output.count)
                                        : 0U;
    result->detections_valid = true;
    result->detection_count = available < kMaxDecodedDetections
                                  ? available
                                  : kMaxDecodedDetections;
    for (std::uint32_t i = 0U; i < result->detection_count; ++i) {
        ProjectDetection(g_postprocess_buffer[i], geometry,
                         &result->detections[i]);
    }
    return {common::ErrorCode::kOk};
}

std::int16_t ClampBoxCoordinate(float value, std::int32_t limit)
{
    if (value <= 0.0F) return 0;
    if (value >= static_cast<float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

common::Error ConvertResult(const ModelResult &source,
                            inference::BoxSet *destination)
{
    if (destination == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    if (!source.detections_valid) {
        return {common::ErrorCode::kModel};
    }

    destination->person = {};
    destination->person.count = source.detection_count <
                                        inference::kMaxBoxes
                                    ? source.detection_count
                                    : inference::kMaxBoxes;
    for (std::uint32_t i = 0U; i < destination->person.count; ++i) {
        const Detection &detection = source.detections[i];
        destination->person.boxes[i].x = ClampBoxCoordinate(
            detection.x, pipeline::kCaptureFormat.width);
        destination->person.boxes[i].y = ClampBoxCoordinate(
            detection.y, pipeline::kCaptureFormat.height);
        destination->person.boxes[i].width = ClampBoxCoordinate(
            detection.width, pipeline::kCaptureFormat.width);
        destination->person.boxes[i].height = ClampBoxCoordinate(
            detection.height, pipeline::kCaptureFormat.height);
        destination->person.boxes[i].confidence = detection.confidence;
    }
    destination->person_valid = true;
    return {common::ErrorCode::kOk};
}

} // namespace

common::Error Future::ConfigureDecoder(const stai_network_info &info)
{
    return InitializeDecoder(info);
}

void Future::Reset(const FutureContext &context,
                   const pipeline::InferenceFrame &frame)
{
    context_ = context;
    frame_ = frame;
    phase_ = Phase::kPreprocess;
    preprocess_stage_logged_ = false;
    infer_stage_logged_ = false;
    postprocess_stage_logged_ = false;
}

bool Future::TryClaim()
{
    bool expected = false;
    return occupied_.compare_exchange_strong(expected, true);
}

void Future::ReleaseClaim()
{
    occupied_.store(false, std::memory_order_release);
}

ai_runtime::AiModelId Future::model_id() const
{
    return static_cast<ai_runtime::AiModelId>(0U);
}

std::uint32_t Future::step_id() const
{
    return static_cast<std::uint32_t>(phase_);
}

common::Error Future::Preprocess()
{
    if (context_.cache == nullptr || context_.info == nullptr) {
        return {common::ErrorCode::kNotInitialized};
    }
    if (!preprocess_stage_logged_) {
        UAI_LOG_INFO("ai: person preprocess begin seq=%u buffer=%x\n",
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(frame_.buffer.address));
    }
    if (!frame_ || !frame_.from_pipe2 ||
        frame_.output_count < context_.info->n_outputs ||
        frame_.buffer.size < context_.info->inputs[0].size_bytes) {
        return {common::ErrorCode::kInvalidArgument};
    }
    frame_.input_prepared_by_cpu = false;
    // The person network is generated for the 480x480 Pipe2 tensor. Keep its
    // input on the DMA-owned inference buffer, as in experiment-ai; the copied
    // source buffer is reserved for models that need CPU resizing.
    const memory_allocator::Buffer &input = frame_.buffer;
    if (!input || input.size < context_.info->inputs[0].size_bytes) {
        return {common::ErrorCode::kInvalidArgument};
    }

    /* Pipe2 wrote this buffer using DMA. A CPU read/invalidate here must not
     * overwrite the DMA image with dirty cache lines. */
    const memory_allocator::Buffer range{
        input.address,
        context_.info->inputs[0].size_bytes,
        input.index,
        memory_allocator::Region::kInference};
    common::Error status = context_.cache->PrepareForCpuRead(range);
    if (status.Ok() && !preprocess_stage_logged_) {
        UAI_LOG_INFO("ai: person preprocess done seq=%u\n",
                     static_cast<unsigned int>(frame_.capture_sequence));
        preprocess_stage_logged_ = true;
    }
    return status;
}

common::Error Future::Infer()
{
    if (context_.npu == nullptr || context_.npu_writer == nullptr ||
        context_.model == nullptr ||
        context_.info == nullptr) {
        return {common::ErrorCode::kNotInitialized};
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO("ai: person infer begin seq=%u\n",
                     static_cast<unsigned int>(frame_.capture_sequence));
    }
    const memory_allocator::Buffer &input = frame_.buffer;
    npu::Status result =
        context_.npu->SelectModel(*context_.model, *context_.npu_writer);
    if (!result.Ok()) return result.error;
    context_.npu->SetEpochTraceModelKindId(context_.model_kind_id,
                                           *context_.npu_writer);

    result = context_.npu->SetInput(
        reinterpret_cast<stai_ptr>(input.address),
        context_.info->inputs[0].size_bytes, *context_.npu_writer);
    if (!result.Ok()) return result.error;

    stai_ptr outputs[memory_manager::kMemoryConfig.model_output_bytes.size()]{};
    for (std::uint16_t i = 0U; i < context_.info->n_outputs; ++i) {
        const auto &output = frame_.outputs[i];
        if (!output || output.size < context_.info->outputs[i].size_bytes ||
            output.alignment == 0U ||
            output.address % output.alignment != 0U) {
            return {common::ErrorCode::kInvalidArgument};
        }
        outputs[i] = reinterpret_cast<stai_ptr>(output.address);
    }
    result = context_.npu->SetOutputs(outputs, context_.info->n_outputs,
                                      *context_.npu_writer);
    if (!result.Ok()) return result.error;
    result = context_.npu->Run(*context_.npu_writer);
    if (!result.Ok()) {
        result.error.LogStatus("person.infer", common::LogLevel::kWarn);
        return result.error;
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO("ai: person infer done seq=%u\n",
                     static_cast<unsigned int>(frame_.capture_sequence));
        infer_stage_logged_ = true;
    }
    result = context_.npu->NewInference(*context_.npu_writer);
    return result.error;
}

common::Error Future::Postprocess()
{
    if (context_.cache == nullptr || context_.info == nullptr ||
        context_.publish == nullptr) {
        return {common::ErrorCode::kNotInitialized};
    }
    ModelOutputView view{};
    view.count = context_.info->n_outputs;
    for (std::uint16_t i = 0U; i < context_.info->n_outputs; ++i) {
        const auto &output = frame_.outputs[i];
        const memory_allocator::Buffer range{
            output.address,
            context_.info->outputs[i].size_bytes,
            output.index,
            memory_allocator::Region::kInference};
        common::Error status = context_.cache->PrepareForCpuRead(range);
        if (!status.Ok()) return status;
        view.tensors[i] = reinterpret_cast<const void *>(output.address);
    }

    InferenceGeometry geometry{};
    geometry.projection = InputProjection::kLetterboxed;
    geometry.frame_width = pipeline::kCaptureFormat.width;
    geometry.frame_height = pipeline::kCaptureFormat.height;
    geometry.model_width = kInputWidth;
    geometry.model_height = kInputHeight;
    geometry.content_height =
        (kInputWidth * pipeline::kInferenceContentFormat.height +
         pipeline::kInferenceContentFormat.width - 1U) /
        pipeline::kInferenceContentFormat.width;
    geometry.pad_top = (geometry.model_height - geometry.content_height) / 2U;

    ModelResult decoded{};
    common::Error status = DecodePerson(view, geometry, &decoded);
    if (!status.Ok()) return status;

    inference::BoxSet boxes{};
    status = ConvertResult(decoded, &boxes);
    if (!status.Ok()) return status;
    context_.publish(context_.publish_context, boxes);
    if (!postprocess_stage_logged_) {
        UAI_LOG_INFO("ai: person postprocess done seq=%u boxes=%u\n",
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(boxes.person.count));
        postprocess_stage_logged_ = true;
    }
    return {};
}

ai_runtime::AiRuntimeResult Future::Evaluate()
{
    common::Error status{};
    switch (phase_) {
    case Phase::kPreprocess:
        status = Preprocess();
        if (status.Ok()) {
            phase_ = Phase::kNpu;
            return {{}, {ai_runtime::ExecutionContext::kNpu}, false};
        }
        break;
    case Phase::kNpu:
        status = Infer();
        if (status.Ok()) {
            phase_ = Phase::kPostprocess;
            return {{}, {ai_runtime::ExecutionContext::kPostprocessCpu},
                    false};
        }
        break;
    case Phase::kPostprocess:
        status = Postprocess();
        return {status, {}, true};
    }
    return {status, {}, false};
}

} // namespace uai::ai::models::person
