#pragma once

#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::camera::dcmipp_resize {

/* STM32N6 DCMIPP policy; no camera HAL calls here. The camera driver applies
 * the chosen decimation/downsize settings to Pipe1 and Pipe2. */
struct Selection {
    std::uint32_t decimation = 1U;
    std::uint32_t input_width = 0U;
    std::uint32_t input_height = 0U;
};

common::Error Select(std::uint32_t input_width, std::uint32_t input_height,
                     std::uint32_t output_width, std::uint32_t output_height,
                     Selection *selection);

} // namespace uai::ai::camera::dcmipp_resize
