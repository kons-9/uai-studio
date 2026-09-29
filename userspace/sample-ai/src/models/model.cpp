#include "models/model.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "image_resizer/image_resizer.hpp"
#include "memory_allocator/memory_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace uai::ai::models {

namespace {

constexpr std::uint32_t kPipe2PadTop = 96U;

common::Error ValidatePipe2Stage(const ModelStageContext &context,
                                 std::uint32_t model_width,
                                 std::uint32_t model_height)
{
    if (context.frame == nullptr || context.cache == nullptr ||
        !context.frame->from_pipe2) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.model_input.invalid_pipe2_frame"};
    }
    if (model_width == 0U || model_height == 0U ||
        !context.frame->scratch ||
        context.frame->scratch.size <
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

} // namespace

common::Error Model::ExecutePipe2InputStage(ModelStageId stage,
                                            ModelStageContext &context,
                                            std::uint32_t model_width,
                                            std::uint32_t model_height)
{
    common::Error status =
        ValidatePipe2Stage(context, model_width, model_height);
    if (!status.Ok()) {
        return status;
    }

    auto &frame = *context.frame;
    constexpr std::size_t kSourceRowBytes =
        static_cast<std::size_t>(
            memory_allocator::kConfig.inference_source_width) * 3U;
    auto *scratch = reinterpret_cast<std::uint8_t *>(frame.scratch.address);
    auto *input = reinterpret_cast<std::uint8_t *>(frame.buffer.address);

    switch (stage) {
    case ModelStageId::kCopy: {
        status = context.cache->PrepareForCpuRead(frame.buffer);
        if (!status.Ok()) {
            return status;
        }
        const auto *pipe2 = reinterpret_cast<const std::uint8_t *>(
                                frame.buffer.address) +
                            kPipe2PadTop * kSourceRowBytes;
        for (std::uint32_t y = 0U;
             y < memory_allocator::kConfig.inference_source_height; ++y) {
            std::memcpy(scratch + static_cast<std::size_t>(y) * kSourceRowBytes,
                        pipe2 + static_cast<std::size_t>(y) * kSourceRowBytes,
                        kSourceRowBytes);
        }
        return {common::ErrorCode::kOk, 0U, "ai.model_input.copy"};
    }
    case ModelStageId::kResize: {
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
    case ModelStageId::kLetterbox:
        return image_resizer::FillRgb888LetterboxPadding(
            {input, model_width, model_height, model_width * 3U},
            model_width, ContentHeight(model_width));
    default:
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(stage),
                "ai.model_input.unsupported_stage"};
    }
}

common::Error Model::PreparePipe2LetterboxedInput(
    memory_allocator::InferenceFrame &frame, std::uint32_t model_width,
    std::uint32_t model_height, cache::CacheDriver &cache)
{
    ModelStageContext context{&frame, &cache};
    common::Error status = ExecutePipe2InputStage(
        ModelStageId::kCopy, context, model_width, model_height);
    if (status.Ok()) {
        status = ExecutePipe2InputStage(ModelStageId::kResize, context,
                                         model_width, model_height);
    }
    if (status.Ok()) {
        status = ExecutePipe2InputStage(ModelStageId::kLetterbox, context,
                                         model_width, model_height);
    }
    if (!status.Ok()) {
        return status;
    }
    frame.input_prepared_by_cpu = true;
    return {common::ErrorCode::kOk, 0U, "ai.model_input.pipe2_letterbox"};
}

} // namespace uai::ai::models
