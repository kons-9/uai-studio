#include "image_resizer/image_resizer.hpp"

#include <gtest/gtest.h>

namespace {

TEST(
    ImageResizer,
    SelectionPolicy
)
{
    uai::ai::image_resizer::Selection selection{};
    ASSERT_TRUE(
        uai::ai::image_resizer::Select(
            {uai::ai::image_resizer::InputKind::kRgb565Memory, 800U, 480U, 128U, 128U}, &selection
        )
            .Ok()
    );
    EXPECT_EQ(selection.hardware, uai::ai::image_resizer::Hardware::kCpu);
    ASSERT_TRUE(
        uai::ai::image_resizer::Select(
            {uai::ai::image_resizer::InputKind::kCameraPipe, 4096U, 2048U, 256U, 256U}, &selection
        )
            .Ok()
    );
    EXPECT_EQ(selection.hardware, uai::ai::image_resizer::Hardware::kDcmipp);
    EXPECT_EQ(selection.dcmipp_decimation, 2U);
    EXPECT_EQ(selection.dcmipp_input_width, 2048U);
    EXPECT_FALSE(
        uai::ai::image_resizer::Select(
            {uai::ai::image_resizer::InputKind::kCameraPipe, 64U, 64U, 128U, 128U}, &selection
        )
            .Ok()
    );
    EXPECT_FALSE(
        uai::ai::image_resizer::Select({uai::ai::image_resizer::InputKind::kCameraPipe, 1U, 1U, 1U, 1U}, nullptr).Ok()
    );
}

} // namespace
