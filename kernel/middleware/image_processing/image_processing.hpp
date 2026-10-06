#ifndef UAI_IMAGE_PROCESSING_HPP
#define UAI_IMAGE_PROCESSING_HPP

#include <cstdint>

#include "middleware/foundation/error.hpp"

namespace uai::ai::image_processing {

struct Rgb565Source {
    const std::uint16_t *pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t stride_pixels = 0U;
};

struct Rgb888Destination {
    std::uint8_t *pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t stride_bytes = 0U;
};

struct Rgb888Source {
    const std::uint8_t *pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t stride_bytes = 0U;
};

// Buffers are caller-owned and must not overlap. Strides are in pixels for
// RGB565 and in bytes for RGB888; no cache maintenance is performed here.
common::Error Resize(
    const Rgb565Source &source, std::uint32_t crop_x, std::uint32_t crop_y,
    std::uint32_t crop_width, std::uint32_t crop_height,
    const Rgb888Destination &destination);

common::Error ResizeLetterbox(
    const Rgb888Source &source, const Rgb888Destination &destination,
    std::uint32_t content_width, std::uint32_t content_height,
    std::uint8_t pad_value = 0U);

common::Error Resize(const Rgb888Source &source,
                         const Rgb888Destination &destination);

common::Error FillLetterboxPadding(
    const Rgb888Destination &destination, std::uint32_t content_width,
    std::uint32_t content_height, std::uint8_t pad_value = 0U);

} // namespace uai::ai::image_processing

#endif // UAI_IMAGE_PROCESSING_HPP