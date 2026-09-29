#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uai::ai::buffer_layout {

/* Pure sizing policy. Board dimensions, output tensors and linker reservations
 * are supplied by the application; no PSRAM address or HAL header lives here. */
struct Config {
    std::uint32_t frame_width;
    std::uint32_t frame_height;
    std::uint32_t frame_bytes_per_pixel;
    std::uint32_t inference_width;
    std::uint32_t inference_height;
    std::uint32_t inference_bytes_per_pixel;
    std::size_t buffer_alignment;
    std::array<std::size_t, 4U> model_output_bytes;
    std::uint32_t inference_source_width;
    std::uint32_t inference_source_height;
    std::size_t max_boxes;

    constexpr std::size_t AlignUp(std::size_t value) const
    {
        return (value + buffer_alignment - 1U) / buffer_alignment *
               buffer_alignment;
    }

    constexpr std::size_t frame_bytes() const
    {
        return static_cast<std::size_t>(frame_width) * frame_height *
               frame_bytes_per_pixel;
    }

    constexpr std::size_t inference_frame_bytes() const
    {
        return static_cast<std::size_t>(inference_width) * inference_height *
               inference_bytes_per_pixel;
    }

    constexpr std::size_t inference_scratch_bytes() const
    {
        return static_cast<std::size_t>(inference_source_width) *
               inference_source_height * inference_bytes_per_pixel;
    }

    constexpr std::size_t inference_source_bytes() const
    {
        return inference_frame_bytes();
    }

    constexpr std::size_t inference_outputs_offset() const
    {
        return AlignUp(inference_frame_bytes());
    }

    constexpr std::size_t inference_output_storage_bytes() const
    {
        std::size_t total = 0U;
        for (const std::size_t output_bytes : model_output_bytes) {
            total += AlignUp(output_bytes);
        }
        return total;
    }

    constexpr std::size_t inference_buffer_bytes() const
    {
        return AlignUp(inference_outputs_offset() +
                       inference_output_storage_bytes());
    }
};

} // namespace uai::ai::buffer_layout
