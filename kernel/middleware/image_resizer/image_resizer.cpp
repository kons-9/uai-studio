#include "image_resizer/image_resizer.hpp"

namespace uai::ai::image_resizer {
namespace {

std::uint32_t CeilDivide(
    std::uint32_t value,
    std::uint32_t divisor
)
{
    return (value + divisor - 1U) / divisor;
}

bool IsValidDimension(
    std::uint32_t width,
    std::uint32_t height
)
{
    return width != 0U && height != 0U;
}

} // namespace

common::Error Select(
    const Request &request,
    Selection *selection
)
{
    if (selection == nullptr || !IsValidDimension(request.input_width, request.input_height)
        || !IsValidDimension(request.output_width, request.output_height)) {
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    *selection = {};
    if (request.input == InputKind::kRgb565Memory) {
        /* DMA2D/GPU2D are intentionally not enabled yet.  CPU is the safe
         * fallback for a frame that already exists in memory. */
        selection->hardware = Hardware::kCpu;
        selection->dcmipp_input_width = request.input_width;
        selection->dcmipp_input_height = request.input_height;
        return {common::ErrorCode::kOk};
    }

    /* DCMIPP's downsize ratio is limited to 8:1.  Select the smallest common
     * decimation factor that keeps both axes within that range.  Using one
     * factor for both axes preserves the requested crop aspect ratio. */
    constexpr std::uint32_t kMaxDecimation = 8U;
    constexpr std::uint32_t kDcmippDownsizeLimit = 8U;
    std::uint32_t decimation = 1U;
    while (decimation <= kMaxDecimation
           && (CeilDivide(request.input_width, decimation) > request.output_width * kDcmippDownsizeLimit
               || CeilDivide(request.input_height, decimation) > request.output_height * kDcmippDownsizeLimit)) {
        decimation *= 2U;
    }
    if (decimation > kMaxDecimation) {
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    const std::uint32_t decimated_width = CeilDivide(request.input_width, decimation);
    const std::uint32_t decimated_height = CeilDivide(request.input_height, decimation);
    if (decimated_width < request.output_width || decimated_height < request.output_height) {
        /* DCMIPP is a downsize path, not an upscaler. */
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    selection->hardware = Hardware::kDcmipp;
    selection->dcmipp_decimation = decimation;
    selection->dcmipp_input_width = decimated_width;
    selection->dcmipp_input_height = decimated_height;
    return {common::ErrorCode::kOk};
}

const char *HardwareName(Hardware hardware)
{
    switch (hardware) {
    case Hardware::kDcmipp:
        return "dcmipp";
    case Hardware::kCpu:
        return "cpu";
    case Hardware::kDma2d:
        return "dma2d-unsupported";
    case Hardware::kGpu2d:
        return "gpu2d-unsupported";
    }
    return "unknown";
}

} // namespace uai::ai::image_resizer
