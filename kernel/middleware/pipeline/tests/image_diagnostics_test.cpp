#include "middleware/pipeline/image_diagnostics.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace uai::ai::pipeline {

TEST(
    ImageDiagnostics,
    CrcAndLuminance
)
{
    constexpr std::uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(Crc32Bytes(text, sizeof(text)), 0xCBF43926U);

    const std::uint16_t rgb565[] = {0xFFFFU, 0U};
    const auto luminance = SampleRgb565Luminance(rgb565, 2U, 1U);
    EXPECT_EQ(luminance.mean, 127U);
    EXPECT_EQ(luminance.peak, 255U);

    const std::uint8_t rgb888[] = {255U, 255U, 255U, 0U, 0U, 0U};
    const auto input = InspectRgb888(rgb888, sizeof(rgb888), 2U, 1U);
    EXPECT_EQ(input.crc, Crc32Bytes(rgb888, sizeof(rgb888)));
    EXPECT_EQ(input.minimum, 0U);
    EXPECT_EQ(input.maximum, 255U);
    EXPECT_EQ(input.mean_luminance, 127U);
}

TEST(
    ImageDiagnostics,
    CaptureRowDistribution
)
{
    std::vector<std::uint16_t> pixels(kCaptureFormat.width * kCaptureFormat.height);
    pixels[kCaptureFormat.width] = 0xFFFFU;
    const auto rows = InspectCaptureRows(pixels.data());
    EXPECT_EQ(rows.nonzero_rows, 1U);
    EXPECT_EQ(rows.zero_rows, kCaptureFormat.height - 1U);
    EXPECT_EQ(rows.first_nonzero, 1U);
    EXPECT_EQ(rows.last_nonzero, 1U);
    EXPECT_EQ(rows.distinct_row_crcs, 2U);
    EXPECT_FALSE(rows.has_data[0U]);
    EXPECT_TRUE(rows.has_data[1U]);
    EXPECT_FALSE(rows.has_data[2U]);
}

} // namespace uai::ai::pipeline