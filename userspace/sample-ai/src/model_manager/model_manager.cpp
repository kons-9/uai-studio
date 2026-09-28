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

#if !defined(AI_MODEL_SEGMENTATION)
constexpr std::size_t kInputCropX = 160U;
constexpr std::size_t kInputSize = 480U;
constexpr std::uint32_t kMaxDetections = 100U;

using Float = float;

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

bool ConvertDetections(stai_ptr *outputs, BoxSet *result)
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
        const Float left = static_cast<Float>(kInputCropX) +
                           (source.x_center - source.width * 0.5F) *
                               static_cast<Float>(kInputSize);
        const Float top =
            (source.y_center - source.height * 0.5F) *
            static_cast<Float>(kInputSize);
        result->boxes[i].x = ClampCoordinate(left, memory_allocator::kFrameWidth);
        result->boxes[i].y = ClampCoordinate(top, memory_allocator::kFrameHeight);
        result->boxes[i].width = ClampCoordinate(
            source.width * static_cast<Float>(kInputSize),
            memory_allocator::kFrameWidth);
        result->boxes[i].height = ClampCoordinate(
            source.height * static_cast<Float>(kInputSize),
            memory_allocator::kFrameHeight);
        result->boxes[i].confidence = source.confidence;
    }
    return true;
}

std::uint32_t ConfidenceMilli(float confidence)
{
    if (confidence <= 0.0F) {
        return 0U;
    }
    return static_cast<std::uint32_t>(confidence * 1000.0F + 0.5F);
}

void LogBoxes(const char *stage, const BoxSet &boxes)
{
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: boxes stage=%s model=%u capture=%u count=%u\n"),
              stage, static_cast<unsigned int>(boxes.model_sequence),
              static_cast<unsigned int>(boxes.capture_sequence),
              static_cast<unsigned int>(boxes.count));
    const std::uint32_t count = boxes.count < memory_allocator::kMaxBoxes
                                    ? boxes.count
                                    : memory_allocator::kMaxBoxes;
    for (std::uint32_t i = 0U; i < count; ++i) {
        const memory_allocator::Box &box = boxes.boxes[i];
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: box stage=%s index=%u x=%d y=%d w=%d h=%d conf_milli=%u\n"),
                  stage, static_cast<unsigned int>(i),
                  static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.width), static_cast<int>(box.height),
                  static_cast<unsigned int>(ConfidenceMilli(box.confidence)));
    }
}
#else
constexpr std::size_t kSegmentationInputWidth = 320U;
constexpr std::size_t kSegmentationInputHeight = 320U;
constexpr std::size_t kSegmentationMaskWidth = 320U;
constexpr std::size_t kSegmentationMaskHeight = 320U;
constexpr std::size_t kSegmentationMaskBytes =
    kSegmentationMaskWidth * kSegmentationMaskHeight;
constexpr std::uintptr_t kSegmentationMaskBuffers[2] = {
    0x91700000UL,
    0x91720000UL,
};

bool ConvertSegmentationMask(stai_ptr output, std::uint8_t mask_index,
                             BoxSet *result)
{
    if (output == nullptr || result == nullptr || mask_index > 1U) {
        return false;
    }
    const auto *logits = reinterpret_cast<const std::int8_t *>(output);
    auto *mask = reinterpret_cast<std::uint8_t *>(
        kSegmentationMaskBuffers[mask_index]);
    for (std::size_t i = 0U; i < kSegmentationMaskBytes; ++i) {
        mask[i] = logits[2U * i + 1U] > logits[2U * i] ? 1U : 0U;
    }
    result->mask_address = kSegmentationMaskBuffers[mask_index];
    result->mask_width = static_cast<std::uint16_t>(kSegmentationMaskWidth);
    result->mask_height = static_cast<std::uint16_t>(kSegmentationMaskHeight);
    return true;
}
#endif

} // namespace

Error ModelManager::Initialize(memory_allocator::MemoryAllocator &memory,
                               cache::CacheDriver &cache)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "ai.initialize"};
    }
    memory_ = &memory;
    cache_ = &cache;
    npu::Status npu_status = npu_.Initialize(model_);
    last_npu_status_ = npu_status;
    if (!npu_status.Ok()) {
        last_error_ = npu_status.error.detail;
        return npu_status.error;
    }

    npu_status = npu_.GetInfo(&info_);
    last_npu_status_ = npu_status;
    last_error_ = npu_status.error.detail;
    const std::uint16_t expected_outputs =
#if defined(AI_MODEL_SEGMENTATION)
        1U;
#else
        3U;
#endif
    if (!npu_status.Ok() || info_.n_inputs != 1U ||
        info_.n_outputs != expected_outputs ||
        info_.inputs == nullptr || info_.outputs == nullptr) {
        return {ErrorCode::kModel, last_error_, "ai.model_info"};
    }

    stai_size output_count = 0U;
    npu_status = npu_.GetOutputs(outputs_, &output_count);
    last_npu_status_ = npu_status;
    if (!npu_status.Ok() || output_count != expected_outputs) {
        last_error_ = npu_status.error.detail;
        return {ErrorCode::kModel, last_error_, "ai.model_outputs"};
    }
#if defined(AI_MODEL_SEGMENTATION)
    if (info_.inputs[0].size_bytes !=
            kSegmentationInputWidth * kSegmentationInputHeight * 3U ||
        info_.outputs[0].size_bytes != kSegmentationMaskBytes * 2U) {
        return {ErrorCode::kModel, 0U, "ai.segmentation_tensor_shape"};
    }
#endif
#if !defined(AI_MODEL_SEGMENTATION)
    if (!InitializePostprocess(info_)) {
        return {ErrorCode::kModel, 0U, "ai.postprocess_initialize"};
    }
#endif

    initialized_ = true;
    last_error_ = 0U;
    return {ErrorCode::kOk, 0U, "ai.initialize"};
}

Error ModelManager::RunNetwork()
{
    const npu::Status status = npu_.Run();
    last_npu_status_ = status;
    last_error_ = status.error.detail;
    return status.error;
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

    result->count = 0U;
    result->capture_sequence = frame.capture_sequence;
    result->model_sequence = ++model_sequence_;
    result->mask_address = 0U;
    result->mask_width = 0U;
    result->mask_height = 0U;
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: input direct begin sequence=%u source=%x size=%u pipe2=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address),
              static_cast<unsigned int>(info_.inputs[0].size_bytes),
              static_cast<unsigned int>(frame.from_pipe2));
    if (!frame.from_pipe2 ||
        frame.buffer.size < info_.inputs[0].size_bytes) {
        return {ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(frame.buffer.size),
                "ai.direct_input"};
    }
    const memory_allocator::Buffer input_buffer{
        frame.buffer.address, info_.inputs[0].size_bytes, frame.buffer.index,
        memory_allocator::Region::kInference};
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: input cache begin sequence=%u size=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(input_buffer.size));
    /* Pipe2 wrote this buffer. Invalidate the CPU cache so NPU sees the
     * completed DMA contents; cleaning here could write stale CPU lines back
     * over the camera image. */
    Error status = cache_->PrepareForCpuRead(input_buffer);
    if (!status.Ok()) {
        return status;
    }
    tm_printf(reinterpret_cast<const UB *>(
              "ai: input cache end sequence=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence));

    const stai_return_code set_input = model_.SetInput(
        reinterpret_cast<stai_ptr>(frame.buffer.address),
        info_.inputs[0].size_bytes);
    if (set_input != STAI_SUCCESS) {
        return {ErrorCode::kModel, static_cast<std::uint32_t>(set_input),
                "ai.set_user_input"};
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: input direct end sequence=%u source=%x\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address));

    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu run call sequence=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence));
    status = RunNetwork();
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: npu run return sequence=%u code=%u detail=%x\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(status.code),
              static_cast<unsigned int>(status.detail));
    if (!status.Ok()) {
        return status;
    }
    for (std::uint16_t i = 0U; i < info_.n_outputs; ++i) {
        const memory_allocator::Buffer output_buffer{
            reinterpret_cast<std::uintptr_t>(outputs_[i]),
            info_.outputs[i].size_bytes, 0U,
            memory_allocator::Region::kInference};
        status = cache_->PrepareForCpuRead(output_buffer);
        if (!status.Ok()) {
            return status;
        }
    }
#if defined(AI_MODEL_SEGMENTATION)
    if (!ConvertSegmentationMask(outputs_[0], mask_buffer_index_, result)) {
        return {ErrorCode::kModel, 0U, "ai.segmentation_postprocess"};
    }
    mask_buffer_index_ ^= 1U;
#else
    if (!ConvertDetections(outputs_, result)) {
        return {ErrorCode::kModel, 0U, "ai.postprocess"};
    }
    LogBoxes("postprocess", *result);
#endif

    const npu::Status npu_status = npu_.NewInference();
    last_npu_status_ = npu_status;
    last_error_ = npu_status.error.detail;
    if (!npu_status.Ok()) {
        return npu_status.error;
    }
    return {ErrorCode::kOk, result->count, "ai.infer"};
}

Error ModelManager::Shutdown()
{
    if (!initialized_) {
        return {ErrorCode::kNotInitialized, 0U, "ai.shutdown"};
    }
    const npu::Status status = npu_.Shutdown();
    last_npu_status_ = status;
    last_error_ = status.error.detail;
    initialized_ = false;
    return status.error;
}

} // namespace uai::ai
