#include "model_manager/model_manager.hpp"

#include <cstddef>
#include <cstdint>

extern "C" {
#include <tm/tmonitor.h>
}

namespace uai::sample2 {

using common::Error;
using common::ErrorCode;

namespace {

using memory_manager::BoxSet;

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

void ConvertInput(std::uintptr_t source_address, stai_ptr destination)
{
    const auto *source =
        reinterpret_cast<const std::uint16_t *>(source_address);
    auto *output = reinterpret_cast<std::uint8_t *>(destination);
    for (std::size_t y = 0U; y < kInputSize; ++y) {
        for (std::size_t x = 0U; x < kInputSize; ++x) {
            const std::uint16_t pixel =
                source[y * memory_manager::kFrameWidth + kInputCropX + x];
            const std::uint8_t red = static_cast<std::uint8_t>(
                ((pixel >> 11U) & 0x1FU) * 255U / 31U);
            const std::uint8_t green = static_cast<std::uint8_t>(
                ((pixel >> 5U) & 0x3FU) * 255U / 63U);
            const std::uint8_t blue =
                static_cast<std::uint8_t>((pixel & 0x1FU) * 255U / 31U);
            const std::size_t offset = (y * kInputSize + x) * 3U;
            output[offset] = red;
            output[offset + 1U] = green;
            output[offset + 2U] = blue;
        }
    }
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
    result->count = count < memory_manager::kMaxBoxes
                        ? count
                        : memory_manager::kMaxBoxes;
    for (std::uint32_t i = 0U; i < result->count; ++i) {
        const OdDetection &source = g_postprocess_buffer[i];
        const Float left = static_cast<Float>(kInputCropX) +
                           (source.x_center - source.width * 0.5F) *
                               static_cast<Float>(kInputSize);
        const Float top =
            (source.y_center - source.height * 0.5F) *
            static_cast<Float>(kInputSize);
        result->boxes[i].x = ClampCoordinate(left, memory_manager::kFrameWidth);
        result->boxes[i].y = ClampCoordinate(top, memory_manager::kFrameHeight);
        result->boxes[i].width = ClampCoordinate(
            source.width * static_cast<Float>(kInputSize),
            memory_manager::kFrameWidth);
        result->boxes[i].height = ClampCoordinate(
            source.height * static_cast<Float>(kInputSize),
            memory_manager::kFrameHeight);
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
    const std::uint32_t count = boxes.count < memory_manager::kMaxBoxes
                                    ? boxes.count
                                    : memory_manager::kMaxBoxes;
    for (std::uint32_t i = 0U; i < count; ++i) {
        const memory_manager::Box &box = boxes.boxes[i];
        tm_printf(reinterpret_cast<const UB *>(
                      "ai: box stage=%s index=%u x=%d y=%d w=%d h=%d conf_milli=%u\n"),
                  stage, static_cast<unsigned int>(i),
                  static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.width), static_cast<int>(box.height),
                  static_cast<unsigned int>(ConfidenceMilli(box.confidence)));
    }
}

} // namespace

Error ModelManager::Initialize(memory_manager::MemoryManager &memory)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U, "ai.initialize"};
    }
    memory_ = &memory;
    npu_driver::Status npu_status = npu_.Initialize(*model_);
    last_npu_status_ = npu_status;
    if (!npu_status.Ok()) {
        last_error_ = npu_status.error.detail;
        return npu_status.error;
    }

    npu_status = npu_.GetInfo(&info_);
    last_npu_status_ = npu_status;
    last_error_ = npu_status.error.detail;
    if (!npu_status.Ok() || info_.n_inputs != 1U || info_.n_outputs != 3U ||
        info_.inputs == nullptr || info_.outputs == nullptr) {
        return {ErrorCode::kModel, last_error_, "ai.model_info"};
    }

    stai_size input_count = 0U;
    stai_size output_count = 0U;
    npu_status = npu_.GetInputs(&input_, &input_count);
    last_npu_status_ = npu_status;
    if (!npu_status.Ok() || input_count != 1U || input_ == nullptr) {
        last_error_ = npu_status.error.detail;
        return {ErrorCode::kModel, last_error_, "ai.model_inputs"};
    }
    npu_status = npu_.GetOutputs(outputs_, &output_count);
    last_npu_status_ = npu_status;
    if (!npu_status.Ok() || output_count != 3U) {
        last_error_ = npu_status.error.detail;
        return {ErrorCode::kModel, last_error_, "ai.model_outputs"};
    }
    if (!InitializePostprocess(info_)) {
        return {ErrorCode::kModel, 0U, "ai.postprocess_initialize"};
    }

    initialized_ = true;
    last_error_ = 0U;
    return {ErrorCode::kOk, 0U, "ai.initialize"};
}

Error ModelManager::RunNetwork()
{
    const npu_driver::Status status = npu_.Run();
    last_npu_status_ = status;
    last_error_ = status.error.detail;
    return status.error;
}

Error ModelManager::TryInfer(
    const memory_manager::InferenceFrame &frame,
    memory_manager::BoxSet *result)
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
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: convert input begin sequence=%u source=%x destination=%x\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address),
              static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(input_)));
    ConvertInput(frame.buffer.address, input_);
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: convert input end sequence=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence));
    Error status;
    const memory_manager::Buffer input_buffer{
        reinterpret_cast<std::uintptr_t>(input_), info_.inputs[0].size_bytes,
        0U, memory_manager::Region::kInference};
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: input cache begin sequence=%u size=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(input_buffer.size));
    status = memory_->PrepareForPeripheralRead(input_buffer);
    if (!status.Ok()) {
        return status;
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "ai: input cache end sequence=%u\n"),
              static_cast<unsigned int>(frame.capture_sequence));

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
        const memory_manager::Buffer output_buffer{
            reinterpret_cast<std::uintptr_t>(outputs_[i]),
            info_.outputs[i].size_bytes, 0U,
            memory_manager::Region::kInference};
        status = memory_->PrepareForCpuRead(output_buffer);
        if (!status.Ok()) {
            return status;
        }
    }
    if (!ConvertDetections(outputs_, result)) {
        return {ErrorCode::kModel, 0U, "ai.postprocess"};
    }
    LogBoxes("postprocess", *result);

    const npu_driver::Status npu_status = npu_.NewInference();
    last_npu_status_ = npu_status;
    last_error_ = npu_status.error.detail;
    if (!npu_status.Ok()) {
        return npu_status.error;
    }
    return {ErrorCode::kOk, result->count, "ai.infer"};
}

Error ModelManager::Shutdown()
{
    if (!initialized_ || model_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "ai.shutdown"};
    }
    const npu_driver::Status status = npu_.Shutdown();
    last_npu_status_ = status;
    last_error_ = status.error.detail;
    initialized_ = false;
    return status.error;
}

} // namespace uai::sample2
