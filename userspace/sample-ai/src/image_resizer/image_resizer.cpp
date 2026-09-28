#include "image_resizer/image_resizer.hpp"

namespace uai::ai::image_resizer {
namespace {

using common::Error;
using common::ErrorCode;

Error Invalid(const char *operation, std::uint32_t detail = 0U)
{
    return {ErrorCode::kInvalidArgument, detail, operation};
}

std::uint32_t CeilDivide(std::uint32_t value, std::uint32_t divisor)
{
    return (value + divisor - 1U) / divisor;
}

bool IsValidDimension(std::uint32_t width, std::uint32_t height)
{
    return width != 0U && height != 0U;
}

} // namespace

Error Select(const Request &request, Selection *selection)
{
    if (selection == nullptr ||
        !IsValidDimension(request.input_width, request.input_height) ||
        !IsValidDimension(request.output_width, request.output_height)) {
        return Invalid("image_resizer.select.invalid_request");
    }

    *selection = {};
    if (request.input == InputKind::kRgb565Memory) {
        /* DMA2D/GPU2D are intentionally not enabled yet.  CPU is the safe
         * fallback for a frame that already exists in memory. */
        selection->hardware = Hardware::kCpu;
        selection->dcmipp_input_width = request.input_width;
        selection->dcmipp_input_height = request.input_height;
        return {ErrorCode::kOk, 0U, "image_resizer.select.cpu"};
    }

    /* DCMIPP's downsize ratio is limited to 8:1.  Select the smallest common
     * decimation factor that keeps both axes within that range.  Using one
     * factor for both axes preserves the requested crop aspect ratio. */
    constexpr std::uint32_t kMaxDecimation = 8U;
    constexpr std::uint32_t kDcmippDownsizeLimit = 8U;
    std::uint32_t decimation = 1U;
    while (decimation <= kMaxDecimation &&
           (CeilDivide(request.input_width, decimation) >
                request.output_width * kDcmippDownsizeLimit ||
            CeilDivide(request.input_height, decimation) >
                request.output_height * kDcmippDownsizeLimit)) {
        decimation *= 2U;
    }
    if (decimation > kMaxDecimation) {
        return Invalid("image_resizer.select.dcmipp_ratio", decimation);
    }

    const std::uint32_t decimated_width =
        CeilDivide(request.input_width, decimation);
    const std::uint32_t decimated_height =
        CeilDivide(request.input_height, decimation);
    if (decimated_width < request.output_width ||
        decimated_height < request.output_height) {
        /* DCMIPP is a downsize path, not an upscaler. */
        return Invalid("image_resizer.select.dcmipp_upscale", decimation);
    }

    selection->hardware = Hardware::kDcmipp;
    selection->dcmipp_decimation = decimation;
    selection->dcmipp_input_width = decimated_width;
    selection->dcmipp_input_height = decimated_height;
    return {ErrorCode::kOk, decimation, "image_resizer.select.dcmipp"};
}

const char *HardwareName(Hardware hardware)
{
    switch (hardware) {
    case Hardware::kDcmipp: return "dcmipp";
    case Hardware::kCpu: return "cpu";
    case Hardware::kDma2d: return "dma2d-unsupported";
    case Hardware::kGpu2d: return "gpu2d-unsupported";
    }
    return "unknown";
}

Error ResizeRgb565ToRgb888(const Rgb565Source &source, std::uint32_t crop_x,
                           std::uint32_t crop_y, std::uint32_t crop_width,
                           std::uint32_t crop_height,
                           const Rgb888Destination &destination)
{
    if (source.pixels == nullptr || destination.pixels == nullptr ||
        !IsValidDimension(source.width, source.height) ||
        !IsValidDimension(destination.width, destination.height) ||
        crop_width == 0U || crop_height == 0U ||
        source.stride_pixels < source.width ||
        destination.stride_bytes < destination.width * 3U ||
        crop_x >= source.width || crop_y >= source.height ||
        crop_width > source.width - crop_x ||
        crop_height > source.height - crop_y) {
        return Invalid("image_resizer.cpu.invalid_request");
    }

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
        const std::uint32_t source_y =
            crop_y + (y * crop_height) / destination.height;
        for (std::uint32_t x = 0U; x < destination.width; ++x) {
            const std::uint32_t source_x =
                crop_x + (x * crop_width) / destination.width;
            const std::uint16_t pixel =
                source.pixels[source_y * source.stride_pixels + source_x];
            const std::size_t offset =
                static_cast<std::size_t>(y) * destination.stride_bytes +
                static_cast<std::size_t>(x) * 3U;
            destination.pixels[offset] = static_cast<std::uint8_t>(
                ((pixel >> 11U) & 0x1FU) * 255U / 31U);
            destination.pixels[offset + 1U] = static_cast<std::uint8_t>(
                ((pixel >> 5U) & 0x3FU) * 255U / 63U);
            destination.pixels[offset + 2U] = static_cast<std::uint8_t>(
                (pixel & 0x1FU) * 255U / 31U);
        }
    }
    return {ErrorCode::kOk, 0U, "image_resizer.cpu"};
}

} // namespace uai::ai::image_resizer
