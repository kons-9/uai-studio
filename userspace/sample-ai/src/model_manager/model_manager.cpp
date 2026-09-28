#include "model_manager/model_manager.hpp"

#include <cstddef>
#include <cstdint>

/* C実装のT-Monitor APIをC++から呼び出すためのCリンケージ。 */
extern "C" {
#include <tm/tmonitor.h>
}

namespace uai::ai {

using common::Error;
using common::ErrorCode;

namespace {

using memory_allocator::BoxSet;
using memory_allocator::DetectionSet;
using memory_allocator::SegmentationSet;
using Float = float;

constexpr std::size_t kInputSize = 480U;
constexpr std::uint32_t kMaxDetections = 100U;

struct OdInput {
    void *raw_l = nullptr;
    void *raw_m = nullptr;
    void *raw_s = nullptr;
};

struct OdDetection {
    Float x_center = 0.0F;
    Float y_center = 0.0F;
    Float width = 0.0F;
    Float height = 0.0F;
    Float confidence = 0.0F;
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
    Float conf_threshold = 0.0F;
    Float iou_threshold = 0.0F;
    const Float *anchors_l = nullptr;
    const Float *anchors_m = nullptr;
    const Float *anchors_s = nullptr;
    std::int32_t nb_detect = 0;
    Float raw_l_scale = 0.0F;
    Float raw_m_scale = 0.0F;
    Float raw_s_scale = 0.0F;
    std::int8_t raw_l_zero_point = 0;
    std::int8_t raw_m_zero_point = 0;
    std::int8_t raw_s_zero_point = 0;
};

/* C実装のYOLOX後処理ライブラリの関数宣言。定義はSTの
 * lib_vision_models_pp (od_pp_st_yolox.c) 側にある。 */
extern "C" {
std::int32_t od_st_yolox_pp_reset(OdParams *params);
std::int32_t od_st_yolox_pp_process_int8(OdInput *input, OdOutput *output,
                                         OdParams *params);
}

OdParams g_postprocess{};
OdDetection g_postprocess_buffer[kMaxDetections]{};
std::size_t g_output_order[3]{};
const Float g_anchors_l[6] = {30.0F, 30.0F, 4.2F, 15.0F, 13.8F, 42.0F};
const Float g_anchors_m[6] = {15.0F, 15.0F, 2.1F, 7.5F, 6.9F, 21.0F};
const Float g_anchors_s[6] = {7.5F, 7.5F, 1.05F, 3.75F, 3.45F, 10.5F};

void SortOutputs(const stai_network_info &info)
{
    for (std::size_t i = 0U; i < 3U; ++i) {
        g_output_order[i] = i;
    }
    for (std::size_t i = 1U; i < 3U; ++i) {
        const std::size_t value = g_output_order[i];
        std::size_t j = i;
        while (j > 0U &&
               info.outputs[g_output_order[j - 1U]].size_bytes >
                   info.outputs[value].size_bytes) {
            g_output_order[j] = g_output_order[j - 1U];
            --j;
        }
        g_output_order[j] = value;
    }
}

bool InitializePostprocess(const stai_network_info &info)
{
    if (info.n_outputs != 3U || info.outputs == nullptr) {
        return false;
    }
    SortOutputs(info);
    g_postprocess = {};
    g_postprocess.nb_classes = 1;
    g_postprocess.nb_anchors = 3;
    g_postprocess.grid_width_l = 60;
    g_postprocess.grid_height_l = 60;
    g_postprocess.grid_width_m = 30;
    g_postprocess.grid_height_m = 30;
    g_postprocess.grid_width_s = 15;
    g_postprocess.grid_height_s = 15;
    g_postprocess.max_boxes_limit = static_cast<std::int32_t>(kMaxDetections);
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
    return od_st_yolox_pp_reset(&g_postprocess) == 0;
}

std::int16_t ClampCoordinate(Float value, std::int32_t limit)
{
    if (value <= 0.0F) {
        return 0;
    }
    if (value >= static_cast<Float>(limit)) {
        return static_cast<std::int16_t>(limit);
    }
    return static_cast<std::int16_t>(value);
}

bool ConvertDetections(stai_ptr *outputs, DetectionSet *result)
{
    OdInput input{};
    input.raw_s = outputs[g_output_order[0]];
    input.raw_m = outputs[g_output_order[1]];
    input.raw_l = outputs[g_output_order[2]];
    OdOutput output{g_postprocess_buffer, 0};
    /* The reference wrapper resets nb_detect before every run. The decoder
     * appends detections starting at this index, so retaining it carries old
     * boxes into the next inference (especially visible when exposure changes). */
    g_postprocess.nb_detect = 0;
    if (od_st_yolox_pp_process_int8(&input, &output, &g_postprocess) != 0) {
        return false;
    }

    const std::uint32_t count = output.count > 0
                                    ? static_cast<std::uint32_t>(output.count)
                                    : 0U;
    result->count = count < memory_allocator::kMaxBoxes
                        ? count
                        : memory_allocator::kMaxBoxes;
    for (std::uint32_t i = 0U; i < result->count; ++i) {
        const OdDetection &source = g_postprocess_buffer[i];
        /* Pipe2 keeps the complete 800x480 crop and letterboxes it into the
         * 480x480 model input. Undo the vertical padding before mapping back to
         * the Pipe1 surface. */
        constexpr Float kContentHeight = 288.0F;
        constexpr Float kPadTop = 96.0F;
        const Float display_x_scale =
            static_cast<Float>(memory_allocator::kFrameWidth) /
            static_cast<Float>(kInputSize);
        const Float display_y_scale =
            static_cast<Float>(memory_allocator::kFrameHeight) /
            kContentHeight;
        const Float left = (source.x_center - source.width * 0.5F) *
                           display_x_scale * static_cast<Float>(kInputSize);
        const Float top = ((source.y_center - source.height * 0.5F) *
                               static_cast<Float>(kInputSize) -
                           kPadTop) *
                          display_y_scale;
        result->boxes[i].x = ClampCoordinate(left, memory_allocator::kFrameWidth);
        result->boxes[i].y = ClampCoordinate(top, memory_allocator::kFrameHeight);
        result->boxes[i].width = ClampCoordinate(
            source.width * display_x_scale * static_cast<Float>(kInputSize),
            memory_allocator::kFrameWidth);
        result->boxes[i].height = ClampCoordinate(
            source.height * static_cast<Float>(kInputSize) * display_y_scale,
            memory_allocator::kFrameHeight);
        result->boxes[i].confidence = source.confidence;
    }
    return true;
}

constexpr std::size_t kSegmentationInputWidth = 320U;
constexpr std::size_t kSegmentationInputHeight = 320U;
constexpr std::size_t kSegmentationMaskWidth = 320U;
constexpr std::size_t kSegmentationMaskHeight = 320U;
constexpr std::size_t kSegmentationMaskBytes =
    kSegmentationMaskWidth * kSegmentationMaskHeight;
constexpr std::uintptr_t kSegmentationMaskBuffers[2] = {
    0x91A00000UL,
    0x91A20000UL,
};

bool ConvertSegmentationMask(stai_ptr output, std::uint8_t mask_index,
                             SegmentationSet *result)
{
    if (output == nullptr || result == nullptr || mask_index > 1U) {
        return false;
    }
    const auto *logits = reinterpret_cast<const std::int8_t *>(output);
    auto *mask = reinterpret_cast<std::uint8_t *>(
        kSegmentationMaskBuffers[mask_index]);
    std::uint32_t foreground_pixels = 0U;
    for (std::size_t i = 0U; i < kSegmentationMaskBytes; ++i) {
        mask[i] = logits[2U * i + 1U] > logits[2U * i] ? 1U : 0U;
        foreground_pixels += mask[i];
    }
    result->mask_address = kSegmentationMaskBuffers[mask_index];
    result->mask_width = static_cast<std::uint16_t>(kSegmentationMaskWidth);
    result->mask_height = static_cast<std::uint16_t>(kSegmentationMaskHeight);
    result->mask_foreground_pixels = foreground_pixels;
    return true;
}

models::ModelOutputSpec BuildOutputSpec(const stai_network_info &info)
{
    models::ModelOutputSpec spec{};
    spec.count = info.n_outputs;
    for (std::uint16_t i = 0U; i < info.n_outputs &&
                              i < models::kMaxModelOutputs;
         ++i) {
        spec.tensors[i].size_bytes = info.outputs[i].size_bytes;
        spec.tensors[i].scale = info.outputs[i].scale.data[0];
        spec.tensors[i].zero_point = info.outputs[i].zeropoint.data[0];
    }
    return spec;
}

models::InferenceGeometry BuildInferenceGeometry(
    const models::ModelDescriptor &descriptor, bool from_pipe2)
{
    models::InferenceGeometry geometry{};
    geometry.projection = from_pipe2
                              ? models::InputProjection::kLetterboxed
                              : models::InputProjection::kCenteredSquare;
    geometry.frame_width = memory_allocator::kFrameWidth;
    geometry.frame_height = memory_allocator::kFrameHeight;
    geometry.model_width = descriptor.input_width;
    geometry.model_height = descriptor.input_height;
    geometry.content_height =
        (descriptor.input_width * memory_allocator::kInferenceSourceHeight +
         memory_allocator::kInferenceSourceWidth - 1U) /
        memory_allocator::kInferenceSourceWidth;
    geometry.pad_top = geometry.model_height > geometry.content_height
                           ? (geometry.model_height - geometry.content_height) /
                                 2U
                           : 0U;
    return geometry;
}

} // namespace

void ModelManager::BuildModelBindings()
{
    bindings_[0] = {ModelKind::kPerson, &person_model_,
                    &models::person::Runtime(person_model_)};
    bindings_[1] = {ModelKind::kSegmentation, &segmentation_model_,
                    &models::segmentation::Runtime(segmentation_model_)};
    bindings_[2] = {ModelKind::kFace, &face_model_,
                    &models::face::Runtime(face_model_)};
    binding_count_ = 3U;
}

const models::ModelDescriptor *ModelManager::CurrentDescriptor() const
{
    return scheduler_.GetDescriptor();
}

Error ModelManager::ConfigureCurrentModel()
{
    const models::ModelDescriptor *descriptor = scheduler_.GetDescriptor();
    if (descriptor == nullptr) {
        return {ErrorCode::kModel, 0U, "ai.model_descriptor"};
    }
    Error status = scheduler_.GetInfo(&info_);
    if (!status.Ok() || info_.n_inputs != 1U || info_.inputs == nullptr ||
        info_.outputs == nullptr || info_.n_outputs == 0U ||
        info_.n_outputs > models::kMaxModelOutputs) {
        return {ErrorCode::kModel, status.detail, "ai.model_info"};
    }

    if (info_.inputs[0].size_bytes !=
        static_cast<std::size_t>(descriptor->input_width) *
            descriptor->input_height * 3U) {
        return {ErrorCode::kModel, 0U, "ai.model_input_shape"};
    }

    stai_size output_count = 0U;
    status = scheduler_.GetOutputs(outputs_, &output_count);
    if (!status.Ok() || output_count != info_.n_outputs) {
        return {ErrorCode::kModel, status.detail, "ai.model_outputs"};
    }

    /* The generated model reports NULL outputs when the application owns the
     * output buffers. This is the only reliable ownership indication; STAI
     * flags are shared by compiler-owned and user-owned output paths. */
    dynamic_outputs_ = false;
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const bool output_is_user_owned = outputs_[i] == nullptr;
        if (i == 0U) {
            dynamic_outputs_ = output_is_user_owned;
        } else if (dynamic_outputs_ != output_is_user_owned) {
            return {ErrorCode::kModel, i, "ai.mixed_output_ownership"};
        }
    }

    bool postprocess_ok = false;
    switch (model_kind_) {
    case ModelKind::kPerson:
        postprocess_ok = InitializePostprocess(info_);
        break;
    case ModelKind::kSegmentation:
        postprocess_ok = info_.n_outputs == 1U &&
                         info_.outputs[0].size_bytes ==
                             kSegmentationMaskBytes * 2U;
        break;
    case ModelKind::kFace:
        postprocess_ok = true;
        break;
    }
    if (!postprocess_ok) {
        return {ErrorCode::kModel, 0U, "ai.postprocess_initialize"};
    }
    const models::ModelCallbacks callbacks = scheduler_.GetCallbacks();
    if (callbacks.configure != nullptr) {
        const models::ModelOutputSpec spec = BuildOutputSpec(info_);
        status = callbacks.configure(spec, callbacks.user_data);
        if (!status.Ok()) {
            return status;
        }
    }
    last_error_ = 0U;
    return {ErrorCode::kOk, 0U, "ai.configure"};
}

Error ModelManager::Initialize(memory_allocator::MemoryAllocator &memory,
                               cache::CacheDriver &cache)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "ai.initialize"};
    }
    memory_ = &memory;
    cache_ = &cache;
    BuildModelBindings();
    Error status = scheduler_.Initialize(bindings_, binding_count_, model_kind_);
    if (!status.Ok()) {
        last_error_ = status.detail;
        return status;
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: model preloaded=%s\n"),
              reinterpret_cast<const UB *>(
                  bindings_[1].model->GetDescriptor().name));
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: model preloaded=%s\n"),
              reinterpret_cast<const UB *>(
                  bindings_[2].model->GetDescriptor().name));
    status = ConfigureCurrentModel();
    if (!status.Ok()) {
        (void)scheduler_.Shutdown();
        last_error_ = status.detail;
        return status;
    }
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "ai.initialize"};
}

Error ModelManager::SwitchModel(ModelKind kind)
{
    if (!initialized_ || memory_ == nullptr || cache_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "ai.switch_model"};
    }
    if (kind == model_kind_) {
        return {ErrorCode::kOk, 0U, "ai.switch_model"};
    }

    const Error select_status = scheduler_.SelectModel(kind);
    if (!select_status.Ok()) {
        last_error_ = select_status.detail;
        return select_status;
    }
    model_kind_ = kind;
    initialized_ = false;
    const Error status = ConfigureCurrentModel();
    if (!status.Ok()) {
        return status;
    }
    initialized_ = true;
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: model switched to %s\n"),
              reinterpret_cast<const UB *>(
                  scheduler_.GetDescriptor()->name));
    return status;
}

Error ModelManager::TryInfer(
    const memory_allocator::InferenceFrame &frame,
    memory_allocator::BoxSet *result)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "ai.infer"};
    }
    if (!frame || result == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U, "ai.infer"};
    }

    result->person = {};
    result->face = {};
    result->segmentation = {};
    result->person_valid = false;
    result->face_valid = false;
    result->segmentation_valid = false;
    result->capture_sequence = frame.capture_sequence;
    result->model_sequence = ++model_sequence_;
    const models::ModelDescriptor *descriptor = scheduler_.GetDescriptor();
    if (descriptor == nullptr) {
        return {ErrorCode::kModel, 0U, "ai.model_descriptor"};
    }
    if (frame.buffer.size < info_.inputs[0].size_bytes) {
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: input rejected buffer=%u required=%u\n"),
                  static_cast<unsigned int>(frame.buffer.size),
                  static_cast<unsigned int>(info_.inputs[0].size_bytes));
        return {ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(frame.buffer.size),
                "ai.direct_input"};
    }
    if (!frame.from_pipe2) {
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: input rejected source_not_pipe2 model=%s\n"),
                  reinterpret_cast<const UB *>(descriptor->name));
        return {ErrorCode::kInvalidArgument, 2U, "ai.direct_input"};
    }
    const memory_allocator::Buffer input_buffer{
        frame.buffer.address, info_.inputs[0].size_bytes, frame.buffer.index,
        memory_allocator::Region::kInference};
    /* Pipe2 wrote its input through DMA. For a dynamically selected smaller
     * model the inference task has rebuilt the tensor in-place from the same
     * frame, so clean that CPU-written input instead. */
    Error status = frame.input_prepared_by_cpu
                       ? cache_->PrepareForPeripheralRead(input_buffer)
                       : frame.from_pipe2
                             ? cache_->PrepareForCpuRead(input_buffer)
                             : cache_->PrepareForPeripheralRead(input_buffer);
    if (!status.Ok()) {
        return status;
    }
    status = scheduler_.SetInput(reinterpret_cast<stai_ptr>(frame.buffer.address),
                                  info_.inputs[0].size_bytes);
    if (!status.Ok()) {
        return status;
    }
    if (dynamic_outputs_) {
        /* The dynamic allocator exposes the maximum four output slots for
         * every model. A model may consume only a prefix of those slots
         * (person=3, segmentation=1), so reject only insufficient capacity. */
        if (frame.output_count < info_.n_outputs) {
            return {ErrorCode::kInvalidArgument, frame.output_count,
                    "ai.dynamic_output_count"};
        }
        stai_ptr dynamic_output_ptrs[memory_allocator::kMaxModelOutputs]{};
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            const memory_allocator::Buffer &output = frame.outputs[i];
            if (!output || output.size < info_.outputs[i].size_bytes ||
                output.alignment == 0U ||
                (output.address % output.alignment) != 0U) {
                return {ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(i),
                        "ai.dynamic_output_buffer"};
            }
            dynamic_output_ptrs[i] =
                reinterpret_cast<stai_ptr>(output.address);
        }
        status = scheduler_.SetOutputs(dynamic_output_ptrs, info_.n_outputs);
        if (!status.Ok()) {
            return status;
        }
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            outputs_[i] = dynamic_output_ptrs[i];
        }
    }

    status = scheduler_.Run();
    last_error_ = status.detail;
    if (!status.Ok()) {
        return status;
    }
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const memory_allocator::Buffer output_buffer =
            dynamic_outputs_
                ? frame.outputs[i]
                : memory_allocator::Buffer{
                      reinterpret_cast<std::uintptr_t>(outputs_[i]),
                      info_.outputs[i].size_bytes, 0U,
                      memory_allocator::Region::kInference};
        status = cache_->PrepareForCpuRead(output_buffer);
        if (!status.Ok()) {
            return status;
        }
    }
    models::ModelResult decoded_result{};
    const models::ModelCallbacks callbacks = scheduler_.GetCallbacks();
    if (callbacks.on_inference_complete != nullptr) {
        models::ModelOutputView output_view{};
        output_view.count = info_.n_outputs;
        for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
            output_view.tensors[i].data = outputs_[i];
            output_view.tensors[i].spec.size_bytes =
                info_.outputs[i].size_bytes;
            output_view.tensors[i].spec.scale =
                info_.outputs[i].scale.data[0];
            output_view.tensors[i].spec.zero_point =
                info_.outputs[i].zeropoint.data[0];
        }
        const models::InferenceGeometry geometry =
            BuildInferenceGeometry(*descriptor, frame.from_pipe2);
        const models::InferenceCompletionContext context{output_view,
                                                          geometry};
        status = callbacks.on_inference_complete(
            context, &decoded_result, callbacks.user_data);
        if (!status.Ok()) {
            return status;
        }
    }
    switch (model_kind_) {
    case ModelKind::kSegmentation:
        if (!ConvertSegmentationMask(outputs_[0], mask_buffer_index_,
                                     &result->segmentation)) {
            return {ErrorCode::kModel, 0U, "ai.segmentation_postprocess"};
        }
        result->segmentation_valid = true;
        mask_buffer_index_ ^= 1U;
        break;
    case ModelKind::kFace:
    {
        if (!decoded_result.detections_valid) {
            return {ErrorCode::kModel, 0U, "ai.face_decoder"};
        }
        result->face.count = decoded_result.detection_count <
                                     memory_allocator::kMaxBoxes
                                 ? decoded_result.detection_count
                                 : memory_allocator::kMaxBoxes;
        for (std::uint32_t i = 0U; i < result->face.count; ++i) {
            const models::Detection &detection = decoded_result.detections[i];
            result->face.boxes[i].x = ClampCoordinate(
                detection.x, memory_allocator::kFrameWidth);
            result->face.boxes[i].y = ClampCoordinate(
                detection.y, memory_allocator::kFrameHeight);
            result->face.boxes[i].width = ClampCoordinate(
                detection.width, memory_allocator::kFrameWidth);
            result->face.boxes[i].height = ClampCoordinate(
                detection.height, memory_allocator::kFrameHeight);
            result->face.boxes[i].confidence = detection.confidence;
        }
        result->face_valid = true;
        break;
    }
    case ModelKind::kPerson:
        if (!ConvertDetections(outputs_, &result->person)) {
            return {ErrorCode::kModel, 0U, "ai.postprocess"};
        }
        result->person_valid = true;
        break;
    }

    status = scheduler_.NewInference();
    if (!status.Ok()) {
        return status;
    }
    const std::uint32_t result_count =
        result->person.count + result->face.count;
    return {ErrorCode::kOk, result_count, "ai.infer"};
}

Error ModelManager::Shutdown()
{
    if (!initialized_) {
        return {ErrorCode::kNotInitialized, 0U, "ai.shutdown"};
    }
    const Error status = scheduler_.Shutdown();
    last_error_ = status.detail;
    initialized_ = false;
    return status;
}

} // namespace uai::ai
