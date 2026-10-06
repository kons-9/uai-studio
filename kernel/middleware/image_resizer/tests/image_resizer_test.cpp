#include "image_resizer/image_resizer.hpp"

#include <gtest/gtest.h>
#include <cstdint>

using namespace uai::ai::image_resizer;

namespace {

TEST(ImageResizer, SelectionPolicy)
{
    Selection selection{};
    ASSERT_TRUE(Select({InputKind::kRgb565Memory, 800U, 480U, 128U, 128U},
                       &selection).Ok());
    EXPECT_EQ(selection.hardware, Hardware::kCpu);
    ASSERT_TRUE(Select({InputKind::kCameraPipe, 4096U, 2048U, 256U, 256U},
                       &selection).Ok());
    EXPECT_EQ(selection.hardware, Hardware::kDcmipp);
    EXPECT_EQ(selection.dcmipp_decimation, 2U);
    EXPECT_EQ(selection.dcmipp_input_width, 2048U);
    EXPECT_FALSE(Select({InputKind::kCameraPipe, 64U, 64U, 128U, 128U},
                        &selection).Ok());
    EXPECT_FALSE(Select({InputKind::kCameraPipe, 1U, 1U, 1U, 1U},
                        nullptr).Ok());
}

TEST(ImageResizer, Rgb565CropAndStride)
{
    const std::uint16_t source[]{0xFFFFU, 0xF800U, 0U,
                                 0x07E0U, 0x001FU, 0U};
    std::uint8_t output[15]{};
    const Rgb565Source input{source, 2U, 2U, 3U};
    const Rgb888Destination destination{output, 2U, 2U, 9U};
    ASSERT_TRUE(ResizeRgb565ToRgb888(input, 0U, 0U, 2U, 2U,
                                     destination).Ok());
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
    EXPECT_FALSE(ResizeRgb565ToRgb888(input, 1U, 0U, 2U, 2U,
                                      destination).Ok());
}

TEST(ImageResizer, LetterboxAndSplitPrimitives)
{
    const std::uint8_t source[]{10U, 20U, 30U, 40U, 50U, 60U};
    std::uint8_t combined[36]{};
    std::uint8_t split[36]{};
    const Rgb888Source input{source, 2U, 1U, 6U};
    const Rgb888Destination full{combined, 2U, 6U, 6U};
    const Rgb888Destination content{split + 12U, 2U, 2U, 6U};
    ASSERT_TRUE(ResizeRgb888Letterbox(input, full, 2U, 2U, 7U).Ok());
    for (auto &byte : split) byte = 99U;
    ASSERT_TRUE(ResizeRgb888(input, content).Ok());
    EXPECT_EQ(split[0], 99U);
    EXPECT_EQ(split[12], 10U);
    EXPECT_EQ(split[15], 40U);
    ASSERT_TRUE(FillRgb888LetterboxPadding({split, 2U, 6U, 6U},
                                           2U, 2U, 7U).Ok());
    for (std::size_t index = 0U; index < 36U; ++index) {
        EXPECT_EQ(combined[index], split[index]) << "byte " << index;
    }
    EXPECT_FALSE(ResizeRgb888Letterbox(input, full, 3U, 2U, 7U).Ok());
}

} // namespace