#pragma once

#include "middleware/buffer/buffer_types.hpp"
#include "middleware/image_processing/blit.hpp"

#include <limits>

namespace uai::ai::camera {

struct Rect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

inline bool Inside(
    const Rect &rectangle,
    std::uint32_t width,
    std::uint32_t height
)
{
    return rectangle.width != 0 && rectangle.height != 0 && rectangle.x < width && rectangle.y < height
        && rectangle.width <= width - rectangle.x && rectangle.height <= height - rectangle.y;
}

struct Geometry {
    std::uint32_t fps = 30;
    bool horizontal = false;
    bool vertical = false;
    Rect crop{0, 0, 1620, 1944};
};

struct State {
    bool auto_exposure = true;
    int compensation = 0;
    std::uint32_t target = 0;
    std::int32_t reported_exposure_us = 0;
    std::int32_t reported_gain_mdB = 0;
    bool auto_white_balance = true;
    std::uint32_t color_temperature = 0;
    Rect statistics{};
    std::uint32_t sensor_width = 0;
    std::uint32_t sensor_height = 0;
};

struct CaptureOutput {
    buffer::Buffer buffer{};
    std::uint16_t width = 400;
    std::uint16_t height = 480;
    image_processing::Format format = image_processing::Format::kRgb565;
};

struct CaptureConfiguration {
    CaptureOutput pipe1{};
    CaptureOutput pipe2{};
    Geometry geometry{};
    bool automatic_recovery = true;
};

inline bool ValidOutput(const CaptureOutput &output)
{
    const auto pixels = static_cast<std::size_t>(output.width) * output.height;
    const auto bytes = pixels * image_processing::PixelBytes(output.format);
    return output.width != 0 && output.width <= 2592 && output.height != 0 && output.height <= 1944
        && (output.format == image_processing::Format::kRgb565 || output.format == image_processing::Format::kRgb888)
        && bytes != 0 && output.buffer.address != 0 && output.buffer.address % 32 == 0 && output.buffer.size >= bytes
        && output.buffer.size % 32 == 0
        && output.buffer.address <= std::numeric_limits<std::uintptr_t>::max() - output.buffer.size;
}

inline bool ValidConfiguration(const CaptureConfiguration &configuration)
{
    const auto &geometry = configuration.geometry;
    if (!ValidOutput(configuration.pipe1) || !ValidOutput(configuration.pipe2) || geometry.fps < 10 || geometry.fps > 30
        || geometry.fps % 5 != 0 || !Inside(geometry.crop, 2592, 1944)
        || geometry.crop.width < configuration.pipe1.width || geometry.crop.height < configuration.pipe1.height
        || geometry.crop.width < configuration.pipe2.width || geometry.crop.height < configuration.pipe2.height) {
        return false;
    }
    const auto &first = configuration.pipe1.buffer;
    const auto &second = configuration.pipe2.buffer;
    return first.address + first.size <= second.address || second.address + second.size <= first.address;
}

}