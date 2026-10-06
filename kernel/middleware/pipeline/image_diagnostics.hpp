#ifndef UAI_AI_PIPELINE_IMAGE_DIAGNOSTICS_HPP
#define UAI_AI_PIPELINE_IMAGE_DIAGNOSTICS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "middleware/pipeline/image_format.hpp"

namespace uai::ai::pipeline {

constexpr std::uint32_t Crc32Bytes(const std::uint8_t *bytes, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0U; index < size; ++index) {
        crc ^= bytes[index];
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

struct LuminanceStatistics {
    std::uint32_t mean = 0U;
    std::uint32_t peak = 0U;
};

inline LuminanceStatistics SampleRgb565Luminance(
    const std::uint16_t *pixels, std::size_t count, std::size_t step)
{
    std::uint32_t sum = 0U;
    std::uint32_t peak = 0U;
    std::uint32_t samples = 0U;
    for (std::size_t index = 0U; index < count; index += step) {
        const std::uint16_t pixel = pixels[index];
        const std::uint32_t red = ((pixel >> 11U) & 0x1FU) * 255U / 31U;
        const std::uint32_t green = ((pixel >> 5U) & 0x3FU) * 255U / 63U;
        const std::uint32_t blue = (pixel & 0x1FU) * 255U / 31U;
        const std::uint32_t luminance =
            (77U * red + 150U * green + 29U * blue) >> 8U;
        sum += luminance;
        peak = luminance > peak ? luminance : peak;
        ++samples;
    }
    return {samples == 0U ? 0U : sum / samples, peak};
}

struct Rgb888Statistics {
    std::uint32_t crc = 0U;
    std::uint8_t minimum = 0xFFU;
    std::uint8_t maximum = 0U;
    std::uint32_t mean_luminance = 0U;
};

inline Rgb888Statistics InspectRgb888(const std::uint8_t *bytes,
                                      std::size_t size, std::size_t pixel_count,
                                      std::size_t sample_step)
{
    Rgb888Statistics result{};
    result.crc = Crc32Bytes(bytes, size);
    for (std::size_t index = 0U; index < size; ++index) {
        result.minimum = bytes[index] < result.minimum ? bytes[index] : result.minimum;
        result.maximum = bytes[index] > result.maximum ? bytes[index] : result.maximum;
    }
    std::uint32_t sum = 0U;
    std::uint32_t samples = 0U;
    for (std::size_t pixel = 0U; pixel < pixel_count; pixel += sample_step) {
        const std::size_t index = pixel * 3U;
        sum += (77U * bytes[index] + 150U * bytes[index + 1U] +
                29U * bytes[index + 2U]) >> 8U;
        ++samples;
    }
    result.mean_luminance = samples == 0U ? 0U : sum / samples;
    return result;
}

inline bool RowHasData(const std::uint16_t *pixels, std::size_t width)
{
    for (std::size_t column = 0U; column < width; ++column) {
        if (pixels[column] != 0U) return true;
    }
    return false;
}

struct CaptureRowStatistics {
    std::uint32_t zero_rows = 0U;
    std::uint32_t nonzero_rows = 0U;
    std::uint32_t first_nonzero = 0U;
    std::uint32_t last_nonzero = 0U;
    std::uint32_t distinct_row_crcs = 0U;
    std::array<std::uint32_t, kCaptureFormat.height> row_crcs{};
    std::array<bool, kCaptureFormat.height> has_data{};
};

inline CaptureRowStatistics InspectCaptureRows(const std::uint16_t *pixels)
{
    CaptureRowStatistics result{};
    result.first_nonzero = kCaptureFormat.height;
    for (std::uint32_t row = 0U; row < kCaptureFormat.height; ++row) {
        const auto *data = pixels + row * kCaptureFormat.width;
        const bool nonzero = RowHasData(data, kCaptureFormat.width);
        result.has_data[row] = nonzero;
        result.row_crcs[row] = Crc32Bytes(
            reinterpret_cast<const std::uint8_t *>(data),
            kCaptureFormat.width * kCaptureFormat.bytes_per_pixel);
        if (nonzero) {
            ++result.nonzero_rows;
            result.first_nonzero = result.first_nonzero < row ? result.first_nonzero : row;
            result.last_nonzero = row;
        } else {
            ++result.zero_rows;
        }
        bool seen = false;
        for (std::uint32_t previous = 0U; previous < row; ++previous) {
            if (result.row_crcs[previous] == result.row_crcs[row]) {
                seen = true;
                break;
            }
        }
        if (!seen) ++result.distinct_row_crcs;
    }
    return result;
}

} // namespace uai::ai::pipeline

#endif