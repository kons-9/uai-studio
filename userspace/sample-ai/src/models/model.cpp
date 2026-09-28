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

} // namespace

common::Error Model::PreparePipe2LetterboxedInput(
    memory_allocator::InferenceFrame &frame, std::uint32_t model_width,
    std::uint32_t model_height, cache::CacheDriver &cache)
{
    if (!frame.from_pipe2) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.model_input.invalid_pipe2_frame"};
    }
    if (!frame.scratch ||
        frame.scratch.size < memory_allocator::kConfig.inference_scratch_bytes()) {
        return {common::ErrorCode::kNoBuffer, 0U,
                "ai.model_input.no_scratch"};
    }

    /* Pipe2 remains in the person-sized 480x480 layout. Copy its live image
     * to scratch before writing the selected smaller model input. */
    common::Error status = cache.PrepareForCpuRead(frame.buffer);
    if (!status.Ok()) {
        return status;
    }
    constexpr std::size_t kSourceRowBytes =
        static_cast<std::size_t>(
            memory_allocator::kConfig.inference_source_width) * 3U;
    const auto *pipe2 = reinterpret_cast<const std::uint8_t *>(
                             frame.buffer.address) +
                         kPipe2PadTop * kSourceRowBytes;
    auto *scratch = reinterpret_cast<std::uint8_t *>(frame.scratch.address);
    for (std::uint32_t y = 0U;
         y < memory_allocator::kConfig.inference_source_height; ++y) {
        std::memcpy(scratch + static_cast<std::size_t>(y) * kSourceRowBytes,
                    pipe2 + static_cast<std::size_t>(y) * kSourceRowBytes,
                    kSourceRowBytes);
    }

    const std::uint32_t content_height =
        (model_width * memory_allocator::kConfig.inference_source_height +
         memory_allocator::kConfig.inference_source_width - 1U) /
        memory_allocator::kConfig.inference_source_width;
    status = image_resizer::ResizeRgb888Letterbox(
        {scratch, memory_allocator::kConfig.inference_source_width,
         memory_allocator::kConfig.inference_source_height,
         static_cast<std::uint32_t>(kSourceRowBytes)},
        {reinterpret_cast<std::uint8_t *>(frame.buffer.address), model_width,
         model_height, model_width * 3U},
        model_width, content_height);
    if (!status.Ok()) {
        return status;
    }
    frame.input_prepared_by_cpu = true;
    return {common::ErrorCode::kOk, 0U, "ai.model_input.pipe2_letterbox"};
}

} // namespace uai::ai::models
