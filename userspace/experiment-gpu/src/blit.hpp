#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace experiment::graphics {

enum class Format { kRgb565, kRgb888 };
enum class Mode { kCopy, kConvert };

struct Image {
    std::uint8_t *data;
    std::size_t bytes;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    Format format;
};

struct Transfer {
    std::uintptr_t source;
    std::uintptr_t destination;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t input_offset;
    std::uint32_t output_offset;
    Format input_format;
    Format output_format;
    Mode mode;
};

inline std::uint32_t PixelBytes(Format format)
{
    switch (format) {
    case Format::kRgb565: return 2;
    case Format::kRgb888: return 3;
    }
    return 0;
}

inline bool Valid(const Image &image)
{
    const auto pixel_bytes = PixelBytes(image.format);
    if (!image.data || pixel_bytes == 0 || image.width == 0 || image.height == 0 ||
        image.stride % pixel_bytes != 0 || std::uint64_t(image.width) * pixel_bytes > image.stride) {
        return false;
    }
    const auto required = std::uint64_t(image.height - 1) * image.stride + std::uint64_t(image.width) * pixel_bytes;
    const auto address = reinterpret_cast<std::uintptr_t>(image.data);
    return required <= image.bytes && image.bytes <= std::numeric_limits<std::uintptr_t>::max() - address;
}

inline bool BuildTransfer(const Image &source, const Image &destination, Transfer &transfer)
{
    if (!Valid(source) || !Valid(destination) || source.width != destination.width || source.height != destination.height ||
        source.width > 0x3fff || source.height > 0xffff) {
        return false;
    }
    const auto source_address = reinterpret_cast<std::uintptr_t>(source.data);
    const auto destination_address = reinterpret_cast<std::uintptr_t>(destination.data);
    if (source_address < destination_address + destination.bytes && destination_address < source_address + source.bytes) {
        return false;
    }
    const auto input_offset = source.stride / PixelBytes(source.format) - source.width;
    const auto output_offset = destination.stride / PixelBytes(destination.format) - destination.width;
    if (input_offset > 0x3fff || output_offset > 0x3fff) {
        return false;
    }
    transfer = {source_address, destination_address, source.width, source.height, input_offset, output_offset,
                source.format, destination.format, source.format == destination.format ? Mode::kCopy : Mode::kConvert};
    return true;
}

inline bool ReferenceBlit(const Image &source, const Image &destination)
{
    Transfer transfer{};
    if (!BuildTransfer(source, destination, transfer)) {
        return false;
    }
    for (std::uint32_t row = 0; row < source.height; ++row) {
        for (std::uint32_t column = 0; column < source.width; ++column) {
            const auto *input = source.data + std::size_t(row) * source.stride + column * PixelBytes(source.format);
            auto *output = destination.data + std::size_t(row) * destination.stride + column * PixelBytes(destination.format);
            if (source.format == destination.format) {
                for (std::uint32_t channel = 0; channel < PixelBytes(source.format); ++channel) {
                    output[channel] = input[channel];
                }
            } else if (source.format == Format::kRgb888) {
                const std::uint16_t packed = ((input[0] >> 3) << 11) | ((input[1] >> 2) << 5) | (input[2] >> 3);
                output[0] = static_cast<std::uint8_t>(packed);
                output[1] = static_cast<std::uint8_t>(packed >> 8);
            } else {
                const std::uint16_t packed = input[0] | (std::uint16_t(input[1]) << 8);
                const auto red = (packed >> 11) & 31;
                const auto green = (packed >> 5) & 63;
                const auto blue = packed & 31;
                output[0] = static_cast<std::uint8_t>((red << 3) | (red >> 2));
                output[1] = static_cast<std::uint8_t>((green << 2) | (green >> 4));
                output[2] = static_cast<std::uint8_t>((blue << 3) | (blue >> 2));
            }
        }
    }
    return true;
}

}