#include "models/model.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "image_resizer/image_resizer.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "pipeline/model_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace uai::ai::models {

namespace {

constexpr std::uint32_t kPipe2PadTop = 96U;

common::Error ValidatePipe2Stage(const memory_allocator::InferenceFrame &frame,
                                 std::uint32_t model_width,
                                 std::uint32_t model_height)
{
    if (!frame.from_pipe2) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.model_input.invalid_pipe2_frame"};
    }
    if (model_width == 0U || model_height == 0U ||
        !frame.scratch ||
        frame.scratch.size <
            memory_allocator::kConfig.inference_scratch_bytes()) {
        return {common::ErrorCode::kNoBuffer, 0U,
                "ai.model_input.no_scratch"};
    }
    return {common::ErrorCode::kOk, 0U, "ai.model_input.stage_valid"};
}

std::uint32_t ContentHeight(std::uint32_t model_width)
{
    return (model_width * memory_allocator::kConfig.inference_source_height +
            memory_allocator::kConfig.inference_source_width - 1U) /
           memory_allocator::kConfig.inference_source_width;
}

common::Error ExecutePipe2InputStage(pipeline::Stage stage,
                                     memory_allocator::InferenceFrame &frame,
                                     cache::CacheDriver &cache,
                                     std::uint32_t model_width,
                                     std::uint32_t model_height)
{
    common::Error status =
        ValidatePipe2Stage(frame, model_width, model_height);
    if (!status.Ok()) {
        return status;
    }

    constexpr std::size_t kSourceRowBytes =
        static_cast<std::size_t>(
            memory_allocator::kConfig.inference_source_width) * 3U;
    auto *scratch = reinterpret_cast<std::uint8_t *>(frame.scratch.address);
    auto *input = reinterpret_cast<std::uint8_t *>(frame.buffer.address);
    const memory_allocator::Buffer &source =
        frame.source_valid ? frame.source : frame.buffer;
    const auto *source_input = reinterpret_cast<const std::uint8_t *>(
        source.address);

    switch (stage) {
    case pipeline::Stage::kCopy: {
        status = cache.PrepareForCpuRead(source);
        if (!status.Ok()) {
            return status;
        }
        const auto *pipe2 = source_input + kPipe2PadTop * kSourceRowBytes;
        for (std::uint32_t y = 0U;
             y < memory_allocator::kConfig.inference_source_height; ++y) {
            std::memcpy(scratch + static_cast<std::size_t>(y) * kSourceRowBytes,
                        pipe2 + static_cast<std::size_t>(y) * kSourceRowBytes,
                        kSourceRowBytes);
        }
        return {common::ErrorCode::kOk, 0U, "ai.model_input.copy"};
    }
    case pipeline::Stage::kResize: {
        const std::uint32_t content_height = ContentHeight(model_width);
        const std::uint32_t pad_y =
            model_height > content_height
                ? (model_height - content_height) / 2U
                : 0U;
        return image_resizer::ResizeRgb888(
            {scratch, memory_allocator::kConfig.inference_source_width,
             memory_allocator::kConfig.inference_source_height,
             static_cast<std::uint32_t>(kSourceRowBytes)},
            {input + static_cast<std::size_t>(pad_y) * model_width * 3U,
             model_width, content_height, model_width * 3U});
    }
    case pipeline::Stage::kLetterbox:
        return image_resizer::FillRgb888LetterboxPadding(
            {input, model_width, model_height, model_width * 3U},
            model_width, ContentHeight(model_width));
    default:
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(stage),
                "ai.model_input.unsupported_stage"};
    }
}

} // namespace

common::Error Model::PreparePipe2LetterboxedInput(
    memory_allocator::InferenceFrame &frame, std::uint32_t model_width,
    std::uint32_t model_height, cache::CacheDriver &cache)
{
    /* The model's input-stage sequence is private to this CPU-task method.
     * Only a completed frame crosses to the NPU task. */
    common::Error status{};
    for (std::size_t i = 0U; i < pipeline::kResized.count; ++i) {
        status = ExecutePipe2InputStage(pipeline::kResized.stages[i].id,
                                        frame, cache, model_width, model_height);
        if (!status.Ok()) {
            return status;
        }
    }
    frame.input_prepared_by_cpu = true;
    return {common::ErrorCode::kOk, 0U, "ai.model_input.pipe2_letterbox"};
}

} // namespace uai::ai::models
