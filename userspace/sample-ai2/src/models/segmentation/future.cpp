#include "models/segmentation/future.hpp"

#include <cstddef>
#include <cstdint>

#include "common/log.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "driver/npu_driver/npu_network.hpp"
#include "image_resizer/image_resizer.hpp"
#include "application/pipeline/image_format.hpp"
#include "memory_manager/memory_config.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "memory_manager/static_memory_layout.hpp"

namespace uai::ai::models::segmentation {

namespace {

constexpr std::size_t kMaskWidth = 20U;
constexpr std::size_t kMaskHeight = 20U;
constexpr std::size_t kMaskBytes = kMaskWidth * kMaskHeight;
constexpr std::size_t kOutputBytes = kMaskBytes * 2U;
constexpr std::uint32_t kModelId = 1U;

using StaticMemoryKey = static_memory_layout::Key;

common::Error Invalid(const char *operation)
{
    return {common::ErrorCode::kModel, 0U, operation};
}

bool g_decoder_initialized = false;

common::Error InitializeDecoder(const stai_network_info &info)
{
    g_decoder_initialized = false;
    if (info.outputs == nullptr || info.n_outputs != 1U ||
        info.outputs[0].size_bytes != kOutputBytes ||
        static_memory_layout::GetRegion(StaticMemoryKey::kSegmentationMask0)
                .size() < kMaskBytes ||
        static_memory_layout::GetRegion(StaticMemoryKey::kSegmentationMask1)
                .size() < kMaskBytes) {
        return Invalid("segmentation.future.decoder.initialize");
    }
    g_decoder_initialized = true;
    return {common::ErrorCode::kOk, 0U,
            "segmentation.future.decoder.initialize"};
}

common::Error DecodeMask(const void *output, std::uint8_t mask_index,
                         inference::BoxSet *boxes)
{
    if (!g_decoder_initialized) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "segmentation.future.decoder.decode"};
    }
    if (output == nullptr || boxes == nullptr || mask_index > 1U) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "segmentation.future.decoder.decode"};
    }

    const auto key = mask_index == 0U ? StaticMemoryKey::kSegmentationMask0
                                      : StaticMemoryKey::kSegmentationMask1;
    const auto &region = static_memory_layout::GetRegion(key);
    auto *mask = reinterpret_cast<std::uint8_t *>(region.address());
    const auto *logits = reinterpret_cast<const std::int8_t *>(output);
    std::uint32_t foreground_pixels = 0U;
    for (std::size_t i = 0U; i < kMaskBytes; ++i) {
        mask[i] = logits[2U * i + 1U] > logits[2U * i] ? 1U : 0U;
        foreground_pixels += mask[i];
    }

    boxes->segmentation.mask_address = region.address();
    boxes->segmentation.mask_width = static_cast<std::uint16_t>(kMaskWidth);
    boxes->segmentation.mask_height = static_cast<std::uint16_t>(kMaskHeight);
    boxes->segmentation.mask_foreground_pixels = foreground_pixels;
    boxes->segmentation_valid = true;
    return {common::ErrorCode::kOk, foreground_pixels,
            "segmentation.future.decoder.decode"};
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
                "segmentation.future.preprocess.context"};
    }
    if (!preprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: segmentation preprocess begin seq=%u buffer=%x\n"),
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(frame_.buffer.address));
    }
    if (!frame_ || !frame_.from_pipe2 || !frame_.source_valid ||
        frame_.output_count < context_.info->n_outputs ||
        context_.info->n_inputs != 1U || context_.info->inputs == nullptr ||
        context_.info->inputs[0].size_bytes != InputBytes() ||
        frame_.buffer.size < InputBytes() ||
        frame_.source.size < memory_manager::kInferenceSourceBytes) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "segmentation.future.frame"};
    }

    common::Error status = context_.cache->PrepareForCpuRead(frame_.source);
    if (!status.Ok()) return status;
    const image_resizer::Rgb888Source source{
        reinterpret_cast<const std::uint8_t *>(frame_.source.address),
        pipeline::kInferenceFormat.width,
        pipeline::kInferenceFormat.height,
        pipeline::kInferenceFormat.width *
            pipeline::kInferenceFormat.bytes_per_pixel};
    const image_resizer::Rgb888Destination destination{
        reinterpret_cast<std::uint8_t *>(frame_.buffer.address), kInputWidth,
        kInputHeight, kInputWidth * 3U};
    status = image_resizer::ResizeRgb888(source, destination);
    if (!status.Ok()) return status;
    frame_.input_prepared_by_cpu = true;
    status = context_.cache->PrepareForPeripheralRead(
        {frame_.buffer.address, InputBytes(), frame_.buffer.index,
         memory_allocator::Region::kInference});
    if (!status.Ok()) return status;

    if (!preprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: segmentation preprocess done seq=%u\n"),
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
                "segmentation.future.infer.context"};
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: segmentation infer begin seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
    }

    npu::Status result = context_.npu->SelectModel(*context_.model);
    if (!result.Ok()) return result.error;
    context_.npu->SetEpochTraceModelKindId(context_.model_kind_id);
    result = context_.npu->SetInput(
        reinterpret_cast<stai_ptr>(frame_.buffer.address), InputBytes());
    if (!result.Ok()) return result.error;

    stai_ptr outputs[memory_manager::kMemoryConfig.model_output_bytes.size()]{};
    for (std::uint16_t i = 0U; i < context_.info->n_outputs; ++i) {
        const auto &output = frame_.outputs[i];
        if (!output || output.size < context_.info->outputs[i].size_bytes ||
            output.alignment == 0U || output.address % output.alignment != 0U) {
            return {common::ErrorCode::kInvalidArgument, i,
                    "segmentation.future.output_buffer"};
        }
        outputs[i] = reinterpret_cast<stai_ptr>(output.address);
    }
    result = context_.npu->SetOutputs(outputs, context_.info->n_outputs);
    if (!result.Ok()) return result.error;
    result = context_.npu->Run();
    if (!result.Ok()) {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "ai: segmentation infer failed code=%u detail=%u op=%s\n"),
                     static_cast<unsigned int>(result.error.code),
                     static_cast<unsigned int>(result.error.detail),
                     result.error.operation);
        return result.error;
    }
    if (!infer_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: segmentation infer done seq=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence));
        infer_stage_logged_ = true;
    }
    result = context_.npu->NewInference();
    return result.error;
}

common::Error Future::Postprocess()
{
    if (context_.cache == nullptr || context_.info == nullptr ||
        context_.publish == nullptr || context_.info->n_outputs != 1U) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "segmentation.future.postprocess.context"};
    }
    const auto &output = frame_.outputs[0];
    const memory_allocator::Buffer range{
        output.address, context_.info->outputs[0].size_bytes, output.index,
        memory_allocator::Region::kInference};
    common::Error status = context_.cache->PrepareForCpuRead(range);
    if (!status.Ok()) return status;

    inference::BoxSet boxes{};
    status = DecodeMask(reinterpret_cast<const void *>(output.address),
                        mask_buffer_index_, &boxes);
    if (!status.Ok()) return status;
    context_.publish(context_.publish_context, boxes);
    if (!postprocess_stage_logged_) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: segmentation postprocess done seq=%u mask_px=%u\n"),
                     static_cast<unsigned int>(frame_.capture_sequence),
                     static_cast<unsigned int>(
                         boxes.segmentation.mask_foreground_pixels));
        postprocess_stage_logged_ = true;
    }
    mask_buffer_index_ ^= 1U;
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

} // namespace uai::ai::models::segmentation
