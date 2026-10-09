#pragma once
#include "blit.hpp"

namespace experiment::graphics {

enum class Operation { kBlit, kFill, kBlend, kResize };
struct Request {
    Operation operation;
    Image source{};
    Image background{};
    Image destination{};
    std::uint32_t color = 0;
    std::uint8_t alpha = 255;
};

inline bool Disjoint(const Image &first, const Image &second)
{
    const auto left = reinterpret_cast<std::uintptr_t>(first.data);
    const auto right = reinterpret_cast<std::uintptr_t>(second.data);
    return left + first.bytes <= right || right + second.bytes <= left;
}

inline bool Validate(const Request &request)
{
    if (!Valid(request.destination)) { return false; }
    if (request.operation == Operation::kFill) { return request.color <= 0xffffff; }
    if (!Valid(request.source) || !Disjoint(request.source, request.destination)) { return false; }
    if (request.operation == Operation::kResize) { return true; }
    Transfer transfer{};
    if (!BuildTransfer(request.source, request.destination, transfer)) { return false; }
    if (request.operation == Operation::kBlit) { return true; }
    return request.operation == Operation::kBlend && Disjoint(request.background, request.destination) &&
        BuildTransfer(request.background, request.destination, transfer);
}

inline std::uint32_t ReadPixel(const Image &image, std::uint32_t column, std::uint32_t row)
{
    const auto *pixel = image.data + std::size_t(row) * image.stride + column * PixelBytes(image.format);
    if (image.format == Format::kRgb888) { return (std::uint32_t(pixel[0]) << 16) | (std::uint32_t(pixel[1]) << 8) | pixel[2]; }
    const auto value = pixel[0] | (std::uint32_t(pixel[1]) << 8);
    const auto red = (value >> 11) & 31, green = (value >> 5) & 63, blue = value & 31;
    return (((red << 3) | (red >> 2)) << 16) | (((green << 2) | (green >> 4)) << 8) | (blue << 3) | (blue >> 2);
}

inline void WritePixel(const Image &image, std::uint32_t column, std::uint32_t row, std::uint32_t color)
{
    auto *pixel = image.data + std::size_t(row) * image.stride + column * PixelBytes(image.format);
    if (image.format == Format::kRgb888) {
        pixel[0] = color >> 16; pixel[1] = color >> 8; pixel[2] = color;
    } else {
        const auto value = ((color >> 8) & 0xf800) | ((color >> 5) & 0x7e0) | ((color >> 3) & 0x1f);
        pixel[0] = value; pixel[1] = value >> 8;
    }
}

inline bool Reference(const Request &request)
{
    if (!Validate(request)) { return false; }
    if (request.operation == Operation::kBlit) { return ReferenceBlit(request.source, request.destination); }
    for (std::uint32_t row = 0; row < request.destination.height; ++row) {
        for (std::uint32_t column = 0; column < request.destination.width; ++column) {
            auto color = request.color;
            if (request.operation == Operation::kResize) {
                const auto source_column = (std::uint64_t(column) * 2 + 1) * request.source.width / (std::uint64_t(request.destination.width) * 2);
                const auto source_row = (std::uint64_t(row) * 2 + 1) * request.source.height / (std::uint64_t(request.destination.height) * 2);
                color = ReadPixel(request.source, source_column, source_row);
            } else if (request.operation == Operation::kBlend) {
                const auto foreground = ReadPixel(request.source, column, row);
                const auto background = ReadPixel(request.background, column, row);
                color = 0;
                for (unsigned shift = 0; shift <= 16; shift += 8) {
                    const auto channel = (((foreground >> shift) & 255) * request.alpha + ((background >> shift) & 255) * (255 - request.alpha)) / 255;
                    color |= channel << shift;
                }
            }
            WritePixel(request.destination, column, row, color);
        }
    }
    return true;
}

}