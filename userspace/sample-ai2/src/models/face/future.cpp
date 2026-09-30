#include "models/face/future.hpp"

#include <cstddef>
#include <cstdint>

#include "common/log.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/npu_network.hpp"
#include "image_resizer/image_resizer.hpp"
#include "application/pipeline/image_format.hpp"
#include "middleware/memory/buffer_types.hpp"
#include "middleware/memory/generated/memory_config.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "arm_math.h"
#include "fd_blazeface_anchors_0.h"
#include "fd_blazeface_anchors_1.h"
#include "fd_blazeface_pp_if.h"

namespace uai::ai::models::face {

namespace {

constexpr std::size_t kBoxes0 = 512U;
constexpr std::size_t kBoxes1 = 384U;
constexpr std::size_t kTotalBoxes = kBoxes0 + kBoxes1;
constexpr std::size_t kKeypoints = 6U;
constexpr std::size_t kExpectedOutputs = 4U;
constexpr std::uint32_t kModelId = 2U;

struct ModelOutputView {
    const void *tensors[memory_manager::kMemoryConfig.model_output_bytes.size()]{ };
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
};

struct ModelResult {
    bool detections_valid = false;
    std::uint32_t detection_count = 0U;
    Detection detections[inference::kMaxBoxes]{};
};

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
std::size_t g_output_order[kExpectedOutputs]{};
bool g_decoder_initialized = false;

common::Error Invalid(const char *operation)
{
    return {common::ErrorCode::kModel, 0U, operation};
}

std::size_t FindOutput(const stai_network_info &info, std::size_t bytes,
                       std::size_t skip)
{
    for (std::size_t i = 0U; i < info.n_outputs; ++i) {
        if (i != skip && info.outputs[i].size_bytes == bytes) {
            return i;
        }
    }
    return kExpectedOutputs;
}

common::Error InitializeDecoder(const stai_network_info &info)
{
    g_decoder_initialized = false;
    if (info.outputs == nullptr || info.n_outputs != kExpectedOutputs ||
        info.n_outputs > memory_manager::kMemoryConfig.model_output_bytes.size()) {
        return Invalid("face.future.decoder.initialize");
    }

    const std::size_t box0 = FindOutput(info, kBoxes0 * 16U,
                                        kExpectedOutputs);
    const std::size_t score0 = FindOutput(info, kBoxes0, box0);
    const std::size_t score1 = FindOutput(info, kBoxes1, box0);
    const std::size_t box1 = FindOutput(info, kBoxes1 * 16U, box0);
    if (box0 == kExpectedOutputs || score0 == kExpectedOutputs ||
        score1 == kExpectedOutputs || box1 == kExpectedOutputs ||
        score1 == score0 || box1 == score0 || box1 == score1) {
        return Invalid("face.future.decoder.output_layout");
    }

    g_output_order[0] = box0;
    g_output_order[1] = score0;
    g_output_order[2] = score1;
    g_output_order[3] = box1;

    g_face_params = {};
    g_face_params.nb_classes = 1;
    g_face_params.nb_keypoints = static_cast<std::int32_t>(kKeypoints);
    g_face_params.nb_detections_0 = static_cast<std::int32_t>(kBoxes0);
    g_face_params.nb_detections_1 = static_cast<std::int32_t>(kBoxes1);
    g_face_params.in_size = Future::kInputWidth;
    g_face_params.max_boxes_limit =
        static_cast<std::int32_t>(inference::kMaxBoxes);
    g_face_params.conf_threshold = 0.35F;
    g_face_params.iou_threshold = 0.3F;
    g_face_params.pAnchors_0 = g_Anchors_0;
    g_face_params.pAnchors_1 = g_Anchors_1;
    g_face_params.boxe_0_scale = info.outputs[box0].scale.data[0];
    g_face_params.boxe_0_zero_point = static_cast<std::uint8_t>(
        info.outputs[box0].zeropoint.data[0]);
    g_face_params.proba_0_scale = info.outputs[score0].scale.data[0];
    g_face_params.proba_0_zero_point = static_cast<std::uint8_t>(
        info.outputs[score0].zeropoint.data[0]);
    g_face_params.boxe_1_scale = info.outputs[box1].scale.data[0];
    g_face_params.boxe_1_zero_point = static_cast<std::uint8_t>(
        info.outputs[box1].zeropoint.data[0]);
    g_face_params.proba_1_scale = info.outputs[score1].scale.data[0];
    g_face_params.proba_1_zero_point = static_cast<std::uint8_t>(
        info.outputs[score1].zeropoint.data[0]);

    for (std::size_t i = 0U; i < kTotalBoxes; ++i) {
        g_face_output[i].pKeyPoints = g_face_keypoints[i];
    }
    if (fd_blazeface_pp_reset(&g_face_params) != 0) {
        return Invalid("face.future.decoder.initialize");
    }
    g_decoder_initialized = true;
    return {common::ErrorCode::kOk, 0U, "face.future.decoder.initialize"};
}

float ClampProjectedCoordinate(float value, std::uint32_t limit)
{
    if (value <= 0.0F) return 0.0F;
    if (value >= static_cast<float>(limit)) return static_cast<float>(limit);
    return value;
}

common::Error DecodeFace(const ModelOutputView &outputs,
                         const InferenceGeometry &geometry,
                         ModelResult *result)
{
    if (!g_decoder_initialized) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "face.future.decoder.decode"};
    }
    if (result == nullptr || outputs.count < kExpectedOutputs ||
        geometry.frame_width == 0U || geometry.frame_height == 0U ||
        geometry.model_height == 0U || geometry.content_height == 0U) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.future.decoder.decode"};
    }

    const void *raw_box0 = outputs.tensors[g_output_order[0]];
    const void *raw_score0 = outputs.tensors[g_output_order[1]];
    const void *raw_score1 = outputs.tensors[g_output_order[2]];
    const void *raw_box1 = outputs.tensors[g_output_order[3]];
    if (raw_box0 == nullptr || raw_score0 == nullptr ||
        raw_score1 == nullptr || raw_box1 == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.future.decoder.outputs"};
    }

    fd_blazeface_pp_in_t input{
        const_cast<void *>(raw_box0), const_cast<void *>(raw_box1),
        const_cast<void *>(raw_score0), const_cast<void *>(raw_score1)};
    fd_pp_out_t output{g_face_output, 0};
    g_face_params.nb_detect = 0;
    if (fd_blazeface_pp_process_int8(&input, &output, &g_face_params) != 0) {
        return Invalid("face.future.decoder.postprocess");
    }

    const std::uint32_t available = output.nb_detect > 0
                                        ? static_cast<std::uint32_t>(
                                              output.nb_detect)
                                        : 0U;
    result->detections_valid = true;
    result->detection_count = available < inference::kMaxBoxes
                                  ? available
                                  : inference::kMaxBoxes;

    for (std::uint32_t i = 0U; i < result->detection_count; ++i) {
        const fd_pp_outBuffer_t &source = g_face_output[i];
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

        result->detections[i].x =
            ClampProjectedCoordinate(left, geometry.frame_width);
        result->detections[i].y =
            ClampProjectedCoordinate(top, geometry.frame_height);
        result->detections[i].width =
            ClampProjectedCoordinate(width, geometry.frame_width);
        result->detections[i].height =
            ClampProjectedCoordinate(height, geometry.frame_height);
        result->detections[i].confidence = source.conf;
    }
    return {common::ErrorCode::kOk, 0U, "face.future.decoder.decode"};
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
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.future.result_destination"};
    }
    if (!source.detections_valid) {
        return {common::ErrorCode::kModel, 0U, "face.future.result"};
    }

    destination->face = {};
    destination->face.count = source.detection_count <
                                      inference::kMaxBoxes
                                  ? source.detection_count
                                  : inference::kMaxBoxes;
    for (std::uint32_t i = 0U; i < destination->face.count; ++i) {
        const Detection &detection = source.detections[i];
        destination->face.boxes[i].x = ClampBoxCoordinate(
            detection.x, pipeline::kCaptureFormat.width);
        destination->face.boxes[i].y = ClampBoxCoordinate(
            detection.y, pipeline::kCaptureFormat.height);
        destination->face.boxes[i].width = ClampBoxCoordinate(
            detection.width, pipeline::kCaptureFormat.width);
        destination->face.boxes[i].height = ClampBoxCoordinate(
            detection.height, pipeline::kCaptureFormat.height);
        destination->face.boxes[i].confidence = detection.confidence;
    }
    destination->face_valid = true;
    return {common::ErrorCode::kOk, destination->face.count,
            "face.future.result"};
}

std::uint32_t LetterboxContentHeight()
{
    return (Future::kInputWidth * pipeline::kInferenceContentFormat.height +
            pipeline::kInferenceContentFormat.width - 1U) /
           pipeline::kInferenceContentFormat.width;
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
    return static_cast<ai_runtime::AiModelId>(kModelId);
}

std::uint32_t Future::step_id() const
{
    return static_cast<std::uint32_t>(phase_);
}

common::Error Future::Preprocess()
{
    if (context_.cache == nullptr || context_.info == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "face.future.preprocess.context"};
    }
    if (!preprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: face preprocess begin seq=%u buffer=%x\n"),
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(frame_.buffer.address));
    }
    if (!frame_ || !frame_.from_pipe2 ||
        frame_.output_count < context_.info->n_outputs ||
        context_.info->n_inputs != 1U || context_.info->inputs == nullptr ||
        context_.info->inputs[0].size_bytes != InputBytes() ||
        frame_.buffer.size < InputBytes()) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "face.future.frame"};
    }

    const std::uint32_t source_width = pipeline::kInferenceFormat.width;
    const std::uint32_t source_height = pipeline::kInferenceFormat.height;
    const memory_allocator::Buffer &input = frame_.buffer;
    if (frame_.source_valid) {
        if (!frame_.source || frame_.source.size <
                                  memory_manager::kInferenceSourceBytes) {
            return {common::ErrorCode::kInvalidArgument, 0U,
                    "face.future.source"};
        }
        common::Error status = context_.cache->PrepareForCpuRead(frame_.source);
        if (!status.Ok()) return status;

        const image_resizer::Rgb888Source source{
            reinterpret_cast<const std::uint8_t *>(frame_.source.address),
            source_width,
            source_height,
            source_width * 3U};
        const image_resizer::Rgb888Destination destination{
            reinterpret_cast<std::uint8_t *>(input.address),
            kInputWidth,
            kInputHeight,
            kInputWidth * 3U};
        status = image_resizer::ResizeRgb888(source, destination);
        if (!status.Ok()) return status;
        frame_.input_prepared_by_cpu = true;
        status = context_.cache->PrepareForPeripheralRead(
            {input.address, InputBytes(), input.index,
             memory_allocator::Region::kInference});
        if (!status.Ok()) return status;
    } else {
        /* Keep the same fallback contract as person: a caller that supplies
         * an already prepared input may use the inference buffer directly. */
        frame_.input_prepared_by_cpu = false;
        const memory_allocator::Buffer range{
            input.address, InputBytes(), input.index,
            memory_allocator::Region::kInference};
        const common::Error status = context_.cache->PrepareForCpuRead(range);
        if (!status.Ok()) return status;
    }

    if (!preprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: face preprocess done seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
        preprocess_stage_logged_ = true;
    }
    return {};
}

common::Error Future::Infer()
{
    if (context_.npu == nullptr || context_.model == nullptr ||
        context_.info == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "face.future.infer.context"};
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: face infer begin seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
    }

    npu::Status result = context_.npu->SelectModel(*context_.model);
    if (!result.Ok()) return result.error;
    context_.npu->SetEpochTraceModelKindId(context_.model_kind_id);

    result = context_.npu->SetInput(
        reinterpret_cast<stai_ptr>(frame_.buffer.address), InputBytes());
    if (!result.Ok()) return result.error;

    stai_ptr outputs[memory_manager::kMemoryConfig.model_output_bytes.size()]{ };
    for (std::uint16_t i = 0U; i < context_.info->n_outputs; ++i) {
        const auto &output = frame_.outputs[i];
        if (!output || output.size < context_.info->outputs[i].size_bytes ||
            output.alignment == 0U || output.address % output.alignment != 0U) {
            return {common::ErrorCode::kInvalidArgument, i,
                    "face.future.output_buffer"};
        }
        outputs[i] = reinterpret_cast<stai_ptr>(output.address);
    }
    result = context_.npu->SetOutputs(outputs, context_.info->n_outputs);
    if (!result.Ok()) return result.error;
    result = context_.npu->Run();
    if (!result.Ok()) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: face infer failed code=%u detail=%u op=%s\n"),
                     static_cast<unsigned int>(result.error.code),
                     static_cast<unsigned int>(result.error.detail),
                     result.error.operation);
        return result.error;
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: face infer done seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
        infer_stage_logged_ = true;
    }
    result = context_.npu->NewInference();
    return result.error;
}

common::Error Future::Postprocess()
{
    if (context_.cache == nullptr || context_.info == nullptr ||
        context_.publish == nullptr) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "face.future.postprocess.context"};
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
    geometry.content_height = LetterboxContentHeight();
    geometry.pad_top = (geometry.model_height - geometry.content_height) / 2U;

    ModelResult decoded{};
    common::Error status = DecodeFace(view, geometry, &decoded);
    if (!status.Ok()) return status;

    inference::BoxSet boxes{};
    status = ConvertResult(decoded, &boxes);
    if (!status.Ok()) return status;
    context_.publish(context_.publish_context, boxes);
    if (!postprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: face postprocess done seq=%u boxes=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(boxes.face.count));
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

} // namespace uai::ai::models::face
