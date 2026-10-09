#include "image_processing/image_processing.hpp"

#include <cstddef>

namespace uai::ai::image_processing {
namespace {

bool IsValidDimension(
    std::uint32_t width,
    std::uint32_t height
)
{
    return width != 0U && height != 0U;
}

} // namespace

common::Error Resize(
    const Rgb565Source &source,
    std::uint32_t crop_x,
    std::uint32_t crop_y,
    std::uint32_t crop_width,
    std::uint32_t crop_height,
    const Rgb888Destination &destination
)
{
    if (source.pixels == nullptr || destination.pixels == nullptr || !IsValidDimension(source.width, source.height)
        || !IsValidDimension(destination.width, destination.height) || crop_width == 0U || crop_height == 0U
        || source.stride_pixels < source.width || destination.stride_bytes < destination.width * 3U
        || crop_x >= source.width || crop_y >= source.height || crop_width > source.width - crop_x
        || crop_height > source.height - crop_y) {
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
        const std::uint32_t source_y = crop_y + (y * crop_height) / destination.height;
        for (std::uint32_t x = 0U; x < destination.width; ++x) {
            const std::uint32_t source_x = crop_x + (x * crop_width) / destination.width;
            const std::uint16_t pixel = source.pixels[source_y * source.stride_pixels + source_x];
            const std::size_t offset =
                static_cast<std::size_t>(y) * destination.stride_bytes + static_cast<std::size_t>(x) * 3U;
            destination.pixels[offset] = static_cast<std::uint8_t>(((pixel >> 11U) & 0x1FU) * 255U / 31U);
            destination.pixels[offset + 1U] = static_cast<std::uint8_t>(((pixel >> 5U) & 0x3FU) * 255U / 63U);
            destination.pixels[offset + 2U] = static_cast<std::uint8_t>((pixel & 0x1FU) * 255U / 31U);
        }
    }
    return {common::ErrorCode::kOk};
}

common::Error ResizeLetterbox(
    const Rgb888Source &source,
    const Rgb888Destination &destination,
    std::uint32_t content_width,
    std::uint32_t content_height,
    std::uint8_t pad_value
)
{
    if (source.pixels == nullptr || destination.pixels == nullptr || !IsValidDimension(source.width, source.height)
        || !IsValidDimension(destination.width, destination.height) || content_width == 0U || content_height == 0U
        || content_width > destination.width || content_height > destination.height
        || source.stride_bytes < source.width * 3U || destination.stride_bytes < destination.width * 3U) {
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
        auto *row = destination.pixels + static_cast<std::size_t>(y) * destination.stride_bytes;
        for (std::uint32_t x = 0U; x < destination.width * 3U; ++x) {
            row[x] = pad_value;
        }
    }

    const std::uint32_t pad_x = (destination.width - content_width) / 2U;
    const std::uint32_t pad_y = (destination.height - content_height) / 2U;
    for (std::uint32_t y = 0U; y < content_height; ++y) {
        const std::uint32_t source_y = (y * source.height) / content_height;
        for (std::uint32_t x = 0U; x < content_width; ++x) {
            const std::uint32_t source_x = (x * source.width) / content_width;
            const auto *source_pixel = source.pixels + static_cast<std::size_t>(source_y) * source.stride_bytes
                + static_cast<std::size_t>(source_x) * 3U;
            auto *destination_pixel = destination.pixels
                + static_cast<std::size_t>(pad_y + y) * destination.stride_bytes
                + static_cast<std::size_t>(pad_x + x) * 3U;
            destination_pixel[0] = source_pixel[0];
            destination_pixel[1] = source_pixel[1];
            destination_pixel[2] = source_pixel[2];
        }
    }
    return {common::ErrorCode::kOk};
}

common::Error Resize(
    const Rgb888Source &source,
    const Rgb888Destination &destination
)
{
    if (source.pixels == nullptr || destination.pixels == nullptr || !IsValidDimension(source.width, source.height)
        || !IsValidDimension(destination.width, destination.height) || source.stride_bytes < source.width * 3U
        || destination.stride_bytes < destination.width * 3U) {
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
        const std::uint32_t source_y = (y * source.height) / destination.height;
        for (std::uint32_t x = 0U; x < destination.width; ++x) {
            const std::uint32_t source_x = (x * source.width) / destination.width;
            const auto *source_pixel = source.pixels + static_cast<std::size_t>(source_y) * source.stride_bytes
                + static_cast<std::size_t>(source_x) * 3U;
            auto *destination_pixel = destination.pixels + static_cast<std::size_t>(y) * destination.stride_bytes
                + static_cast<std::size_t>(x) * 3U;
            destination_pixel[0] = source_pixel[0];
            destination_pixel[1] = source_pixel[1];
            destination_pixel[2] = source_pixel[2];
        }
    }
    return {common::ErrorCode::kOk};
}

common::Error FillLetterboxPadding(
    const Rgb888Destination &destination,
    std::uint32_t content_width,
    std::uint32_t content_height,
    std::uint8_t pad_value
)
{
    if (destination.pixels == nullptr || !IsValidDimension(destination.width, destination.height) || content_width == 0U
        || content_height == 0U || content_width > destination.width || content_height > destination.height
        || destination.stride_bytes < destination.width * 3U) {
        return common::Error{common::ErrorCode::kInvalidArgument};
    }

    const std::uint32_t pad_x = (destination.width - content_width) / 2U;
    const std::uint32_t pad_y = (destination.height - content_height) / 2U;
    for (std::uint32_t y = 0U; y < destination.height; ++y) {
        auto *row = destination.pixels + static_cast<std::size_t>(y) * destination.stride_bytes;
        for (std::uint32_t x = 0U; x < destination.width; ++x) {
            if (x >= pad_x && x < pad_x + content_width && y >= pad_y && y < pad_y + content_height) {
                continue;
            }
            row[static_cast<std::size_t>(x) * 3U] = pad_value;
            row[static_cast<std::size_t>(x) * 3U + 1U] = pad_value;
            row[static_cast<std::size_t>(x) * 3U + 2U] = pad_value;
        }
    }
    return {common::ErrorCode::kOk};
}

} // namespace uai::ai::image_processing