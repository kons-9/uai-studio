#pragma once

#include <cstdint>

#include "middleware/foundation/error.hpp"

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

/* Backend selection is deliberately independent from HAL register writes.
 * This keeps the policy testable and lets the camera driver apply the
 * returned DCMIPP plan to either Pipe1 or Pipe2. */
common::Error Select(const Request &request, Selection *selection);

const char *HardwareName(Hardware hardware);

} // namespace uai::ai::image_resizer
