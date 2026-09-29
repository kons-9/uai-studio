#include "driver/camera_driver/dcmipp_resize.hpp"

#include <cstdint>

namespace uai::ai::camera::dcmipp_resize {
namespace {

std::uint32_t CeilDivide(std::uint32_t value, std::uint32_t divisor)
{
    return value / divisor + (value % divisor != 0U ? 1U : 0U);
}

} // namespace

common::Error Select(std::uint32_t input_width, std::uint32_t input_height,
                     std::uint32_t output_width, std::uint32_t output_height,
                     Selection *selection)
{
    if (selection == nullptr || input_width == 0U || input_height == 0U ||
        output_width == 0U || output_height == 0U) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "camera.dcmipp_resize.invalid_request"};
    }

    /* DCMIPP supports power-of-two 1/2/4/8 decimation before an 8:1
     * downsize. Keep the same factor on both axes to preserve the crop. */
    std::uint32_t factor = 1U;
    while (factor <= 8U &&
           (static_cast<std::uint64_t>(CeilDivide(input_width, factor)) >
                static_cast<std::uint64_t>(output_width) * 8U ||
            static_cast<std::uint64_t>(CeilDivide(input_height, factor)) >
                static_cast<std::uint64_t>(output_height) * 8U)) {
        factor *= 2U;
    }
    if (factor > 8U || CeilDivide(input_width, factor) < output_width ||
        CeilDivide(input_height, factor) < output_height) {
        return {common::ErrorCode::kInvalidArgument, factor,
                "camera.dcmipp_resize.unsupported_ratio"};
    }
    *selection = {factor, CeilDivide(input_width, factor),
                  CeilDivide(input_height, factor)};
    return {common::ErrorCode::kOk, factor, "camera.dcmipp_resize"};
}

} // namespace uai::ai::camera::dcmipp_resize
