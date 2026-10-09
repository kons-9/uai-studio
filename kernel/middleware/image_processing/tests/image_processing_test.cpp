#include "image_processing/image_processing.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>

namespace {

TEST(
    ImageProcessing,
    Rgb565CropAndStride
)
{
    const std::uint16_t source[]{0xFFFFU, 0xF800U, 0U, 0x07E0U, 0x001FU, 0U};
    std::uint8_t output[15]{};
    const uai::ai::image_processing::Rgb565Source input{source, 2U, 2U, 3U};
    const uai::ai::image_processing::Rgb888Destination destination{output, 2U, 2U, 9U};
    ASSERT_TRUE(uai::ai::image_processing::Resize(input, 0U, 0U, 2U, 2U, destination).Ok());
    EXPECT_EQ(output[0], 255U);
    EXPECT_EQ(output[1], 255U);
    EXPECT_EQ(output[2], 255U);
    EXPECT_EQ(output[3], 255U);
    EXPECT_EQ(output[4], 0U);
    EXPECT_EQ(output[5], 0U);
    EXPECT_EQ(output[9], 0U);
    EXPECT_EQ(output[10], 255U);
    EXPECT_EQ(output[11], 0U);
    EXPECT_EQ(output[12], 0U);
    EXPECT_EQ(output[13], 0U);
    EXPECT_EQ(output[14], 255U);
    EXPECT_FALSE(uai::ai::image_processing::Resize(input, 1U, 0U, 2U, 2U, destination).Ok());
}

TEST(
    ImageProcessing,
    Rgb565EdgeCropUpscalePreservesGuards
)
{
    const std::uint16_t pixels[]{0U, 0U, 0U, 0x1234U, 0U, 0xF800U, 0x07E0U, 0x1234U, 0U, 0x001FU, 0xFFFFU, 0x1234U};
    std::array<std::uint8_t, 35U> output{};
    output.fill(0xA5U);
    const uai::ai::image_processing::Rgb565Source source{pixels, 3U, 3U, 4U};
    const uai::ai::image_processing::Rgb888Destination destination{output.data() + 1U, 3U, 3U, 11U};
    ASSERT_TRUE(uai::ai::image_processing::Resize(source, 1U, 1U, 2U, 2U, destination).Ok());
    const std::uint8_t expected[][9]{
        {255U, 0U, 0U, 255U, 0U, 0U, 0U, 255U, 0U},
        {255U, 0U, 0U, 255U, 0U, 0U, 0U, 255U, 0U},
        {0U, 0U, 255U, 0U, 0U, 255U, 255U, 255U, 255U}
    };
    EXPECT_EQ(output.front(), 0xA5U);
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t byte = 0U; byte < 9U; ++byte) {
            EXPECT_EQ(output[1U + row * 11U + byte], expected[row][byte]);
        }
        EXPECT_EQ(output[1U + row * 11U + 9U], 0xA5U);
        EXPECT_EQ(output[1U + row * 11U + 10U], 0xA5U);
    }
    EXPECT_EQ(output.back(), 0xA5U);
    EXPECT_EQ(
        uai::ai::image_processing::Resize(source, 2U, 1U, 2U, 2U, destination).Code(),
        uai::ai::common::ErrorCode::kInvalidArgument
    );
}

TEST(
    ImageProcessing,
    LetterboxAndSplitPrimitives
)
{
    const std::uint8_t source[]{10U, 20U, 30U, 40U, 50U, 60U};
    std::uint8_t combined[36]{};
    std::uint8_t split[36]{};
    const uai::ai::image_processing::Rgb888Source input{source, 2U, 1U, 6U};
    const uai::ai::image_processing::Rgb888Destination full{combined, 2U, 6U, 6U};
    const uai::ai::image_processing::Rgb888Destination content{split + 12U, 2U, 2U, 6U};
    ASSERT_TRUE(uai::ai::image_processing::ResizeLetterbox(input, full, 2U, 2U, 7U).Ok());
    for (auto &byte : split)
        byte = 99U;
    ASSERT_TRUE(uai::ai::image_processing::Resize(input, content).Ok());
    EXPECT_EQ(split[0], 99U);
    EXPECT_EQ(split[12], 10U);
    EXPECT_EQ(split[15], 40U);
    ASSERT_TRUE(uai::ai::image_processing::FillLetterboxPadding({split, 2U, 6U, 6U}, 2U, 2U, 7U).Ok());
    for (std::size_t index = 0U; index < 36U; ++index) {
        EXPECT_EQ(combined[index], split[index]) << "byte " << index;
    }
    EXPECT_FALSE(uai::ai::image_processing::ResizeLetterbox(input, full, 3U, 2U, 7U).Ok());
}

TEST(
    ImageProcessing,
    Rgb888OddDownscaleAndInvalidInput
)
{
    const std::uint8_t pixels[]{1U, 1U, 1U, 2U, 2U, 2U, 3U, 3U, 3U, 4U, 4U, 4U, 5U, 5U,
                                5U, 6U, 6U, 6U, 7U, 7U, 7U, 8U, 8U, 8U, 9U, 9U, 9U};
    std::array<std::uint8_t, 16U> output{};
    output.fill(0xA5U);
    const uai::ai::image_processing::Rgb888Source source{pixels, 3U, 3U, 9U};
    const uai::ai::image_processing::Rgb888Destination destination{output.data() + 1U, 2U, 2U, 7U};
    ASSERT_TRUE(uai::ai::image_processing::Resize(source, destination).Ok());
    const std::array<std::uint8_t, 16U> expected{
        0xA5U, 1U, 1U, 1U, 2U, 2U, 2U, 0xA5U, 4U, 4U, 4U, 5U, 5U, 5U, 0xA5U, 0xA5U
    };
    EXPECT_EQ(output, expected);
    EXPECT_EQ(
        uai::ai::image_processing::Resize({nullptr, 3U, 3U, 9U}, destination).Code(),
        uai::ai::common::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        uai::ai::image_processing::Resize(source, {destination.pixels, 0U, 2U, 7U}).Code(),
        uai::ai::common::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        uai::ai::image_processing::Resize(source, {destination.pixels, 2U, 2U, 5U}).Code(),
        uai::ai::common::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(output, expected);
}

TEST(
    ImageProcessing,
    SinglePixelLetterboxPreservesRowEnds
)
{
    const std::uint8_t pixel[]{8U, 9U, 10U};
    std::array<std::uint8_t, 50U> output{};
    output.fill(0xA5U);
    const uai::ai::image_processing::Rgb888Source source{pixel, 1U, 1U, 3U};
    const uai::ai::image_processing::Rgb888Destination destination{output.data() + 1U, 5U, 3U, 16U};
    ASSERT_TRUE(uai::ai::image_processing::ResizeLetterbox(source, destination, 1U, 1U, 7U).Ok());
    EXPECT_EQ(output.front(), 0xA5U);
    EXPECT_EQ(output.back(), 0xA5U);
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t byte = 0U; byte < 15U; ++byte) {
            const std::uint8_t expected = row == 1U && byte >= 6U && byte < 9U ? pixel[byte - 6U] : 7U;
            EXPECT_EQ(output[1U + row * 16U + byte], expected);
        }
        EXPECT_EQ(output[1U + row * 16U + 15U], 0xA5U);
    }
    const auto snapshot = output;
    EXPECT_EQ(
        uai::ai::image_processing::ResizeLetterbox(source, destination, 6U, 1U).Code(),
        uai::ai::common::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        uai::ai::image_processing::FillLetterboxPadding(destination, 0U, 1U).Code(),
        uai::ai::common::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(output, snapshot);
}

} // namespace
