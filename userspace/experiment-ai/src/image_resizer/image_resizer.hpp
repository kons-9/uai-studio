#pragma once

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::image_resizer {

/* Keep the hardware choice explicit.  DMA2D and GPU2D are reserved for a
 * later implementation; they must not be selected implicitly as a fallback. */
enum class Hardware : std::uint8_t {
    kDcmipp,
    kCpu,
    kDma2d,
    kGpu2d,
};

enum class InputKind : std::uint8_t {
    kCameraPipe,
    kRgb565Memory,
};

struct Request {
    InputKind input = InputKind::kRgb565Memory;
    std::uint32_t input_width = 0U;
    std::uint32_t input_height = 0U;
    std::uint32_t output_width = 0U;
    std::uint32_t output_height = 0U;
};

struct Selection {
    Hardware hardware = Hardware::kCpu;
    /* DCMIPP supports power-of-two 1/2/4/8 decimation before its downsize
     * stage.  A factor of one means that decimation is disabled. */
    std::uint32_t dcmipp_decimation = 1U;
    std::uint32_t dcmipp_input_width = 0U;
    std::uint32_t dcmipp_input_height = 0U;
};

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

/* Backend selection is deliberately independent from HAL register writes.
 * This keeps the policy testable and lets the camera driver apply the
 * returned DCMIPP plan to either Pipe1 or Pipe2. */
common::Error Select(const Request &request, Selection *selection);

const char *HardwareName(Hardware hardware);

/* CPU fallback for a memory-resident RGB565 frame.  The crop is expressed in
 * source pixels and is converted to RGB888 using nearest-neighbour sampling.
 * No allocation is performed; the caller owns both buffers and cache state. */
common::Error ResizeRgb565ToRgb888(
    const Rgb565Source &source, std::uint32_t crop_x, std::uint32_t crop_y,
    std::uint32_t crop_width, std::uint32_t crop_height,
    const Rgb888Destination &destination);

/* Resize an RGB888 source into a letterboxed RGB888 tensor. The source is
 * sampled with nearest-neighbour interpolation, while the unused padding is
 * filled with pad_value. No allocation is performed. */
common::Error ResizeRgb888Letterbox(
    const Rgb888Source &source, const Rgb888Destination &destination,
    std::uint32_t content_width, std::uint32_t content_height,
    std::uint8_t pad_value = 0U);

/* The two primitives below are the split form of ResizeRgb888Letterbox().
 * They are kept separate so the model pipeline can measure the resize and
 * padding work independently.  ResizeRgb888() writes only the content area;
 * FillRgb888LetterboxPadding() never touches that area. */
common::Error ResizeRgb888(const Rgb888Source &source,
                           const Rgb888Destination &destination);

common::Error FillRgb888LetterboxPadding(
    const Rgb888Destination &destination, std::uint32_t content_width,
    std::uint32_t content_height, std::uint8_t pad_value = 0U);

} // namespace uai::ai::image_resizer
