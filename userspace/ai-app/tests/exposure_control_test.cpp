#include "exposure_control/controller.hpp"
#include "exposure_control/runtime.hpp"

#include <gtest/gtest.h>

namespace {
using namespace uai::ai;

exposure_control::Mapping Mapping()
{
    exposure_control::Mapping mapping{2000, 1200, {0, 0, 2000, 1200}};
    mapping.detection_content = mapping.content;
    return mapping;
}

inference::BoxSet Face(std::uint32_t sequence = 1)
{
    inference::BoxSet result{};
    result.face_valid = true;
    result.capture_sequence = sequence;
    result.face.count = 1;
    result.face.boxes[0] = {120, 168, 96, 96, 0.9F};
    return result;
}

TEST(
    ExposureControl,
    MapsLetterboxAndExpiresWithoutResults
)
{
    exposure_control::Controller controller;
    controller.Observe(Face(), 100);
    EXPECT_TRUE(controller.Step(100, Mapping()));
    const auto &values = controller.DisplayValues();
    EXPECT_EQ(values.source, exposure_control::Source::kFace);
    EXPECT_EQ(values.requested.x, 400U);
    EXPECT_EQ(values.requested.y, 200U);
    EXPECT_EQ(values.requested.width, 600U);
    EXPECT_EQ(values.requested.height, 600U);
    EXPECT_FALSE(controller.Step(1099, Mapping()));
    EXPECT_TRUE(controller.Step(1100, Mapping()));
    EXPECT_EQ(values.source, exposure_control::Source::kFullFrame);
    EXPECT_EQ(values.requested.width, 2000U);
}

TEST(
    ExposureControl,
    PreservesFacePriorityAcrossAlternatingModels
)
{
    exposure_control::Controller controller;
    controller.Observe(Face(), 100);
    auto person = Face(2);
    person.face_valid = false;
    person.person_valid = true;
    person.person = person.face;
    controller.Observe(person, 200);
    EXPECT_TRUE(controller.Step(200, Mapping()));
    EXPECT_EQ(controller.DisplayValues().source, exposure_control::Source::kFace);
    EXPECT_TRUE(controller.Step(1100, Mapping()));
    EXPECT_EQ(controller.DisplayValues().source, exposure_control::Source::kPerson);
}

TEST(
    ExposureControl,
    RejectsStaleResultsAndIgnoresSmallMovement
)
{
    exposure_control::Controller controller;
    controller.Observe(Face(2), 100);
    ASSERT_TRUE(controller.Step(100, Mapping()));
    auto moved = Face(3);
    moved.face.boxes[0].x += 2;
    controller.Observe(moved, 200);
    EXPECT_FALSE(controller.Step(200, Mapping()));
    controller.Observe(Face(1), 1000);
    EXPECT_TRUE(controller.Step(1200, Mapping()));
    EXPECT_EQ(controller.DisplayValues().source, exposure_control::Source::kFullFrame);
}

TEST(
    ExposureControl,
    TimeoutSurvivesClockWrap
)
{
    exposure_control::Controller controller;
    controller.Observe(Face(), 0xffffff00U);
    ASSERT_TRUE(controller.Step(0xffffff00U, Mapping()));
    EXPECT_FALSE(controller.Step(0x100U, Mapping()));
    EXPECT_TRUE(controller.Step(0x300U, Mapping()));
}

TEST(
    ExposureControl,
    MapsCropAndHorizontalFlip
)
{
    exposure_control::Controller controller;
    auto mapping = Mapping();
    mapping.crop = {100, 100, 1000, 600};
    mapping.horizontal = true;
    controller.Observe(Face(), 0);
    ASSERT_TRUE(controller.Step(0, mapping));
    EXPECT_EQ(controller.DisplayValues().requested.x, 600U);
    EXPECT_EQ(controller.DisplayValues().requested.y, 200U);
}

TEST(
    ExposureControl,
    UsesCaptureCoordinatesForDetections
)
{
    exposure_control::Controller controller;
    auto mapping = Mapping();
    mapping.detection_content = {0, 0, 800, 480};
    controller.Observe(Face(), 0);
    ASSERT_TRUE(controller.Step(0, mapping));
    EXPECT_EQ(controller.DisplayValues().requested.x, 240U);
    EXPECT_EQ(controller.DisplayValues().requested.y, 360U);
    EXPECT_EQ(controller.DisplayValues().requested.width, 360U);
}

TEST(
    ExposureControl,
    EmptyResultsDoNotExtendSubjectLifetime
)
{
    exposure_control::Controller controller;
    controller.Observe(Face(), 100);
    ASSERT_TRUE(controller.Step(100, Mapping()));
    auto empty = Face(2);
    empty.face.count = 0;
    controller.Observe(empty, 500);
    EXPECT_FALSE(controller.Step(500, Mapping()));
    EXPECT_TRUE(controller.Step(1100, Mapping()));
}

TEST(
    ExposureControl,
    SelectsForegroundAndRejectsPadding
)
{
    exposure_control::Controller controller;
    inference::BoxSet mask{};
    mask.capture_sequence = 1;
    mask.segmentation_valid = true;
    mask.segmentation.mask_width = 20;
    mask.segmentation.mask_height = 20;
    mask.segmentation.mask_foreground_pixels = 4;
    for (std::uint32_t index = 0; index < 4; ++index)
        mask.segmentation.mask.bytes[9 * 20 + index + 8] = 1;
    controller.Observe(mask, 0);
    EXPECT_TRUE(controller.Step(0, Mapping()));
    EXPECT_EQ(controller.DisplayValues().source, exposure_control::Source::kForeground);
    mask.capture_sequence = 2;
    mask.segmentation.mask = {};
    for (std::uint32_t index = 0; index < 4; ++index)
        mask.segmentation.mask.bytes[index] = 1;
    controller.Observe(mask, 100);
    EXPECT_TRUE(controller.Step(100, Mapping()));
    EXPECT_EQ(controller.DisplayValues().source, exposure_control::Source::kFullFrame);
}

TEST(
    ExposureControl,
    KeepsRequestedAndAppliedValuesSeparate
)
{
    exposure_control::Controller controller;
    controller.Observe(Face(), 0);
    ASSERT_TRUE(controller.Step(0, Mapping()));
    controller.Readback({0, 0, 2000, 1200}, 12000, 3000, true);
    EXPECT_NE(controller.DisplayValues().requested.width, controller.DisplayValues().applied.width);
    char text[64]{};
    exposure_control::Controller::Format(controller.DisplayValues(), text, sizeof(text));
    EXPECT_STREQ(text, "AE FACE 12000us 3000mdB");
    controller.Failed(7);
    exposure_control::Controller::Format(controller.DisplayValues(), text, sizeof(text));
    EXPECT_STREQ(text, "AE ERROR 7");
}

struct FakeCamera {
    camera::State state{};
    camera::Geometry geometry{30, false, false, {0, 0, 2000, 1200}};
    bool fail = false;
    std::uint32_t writes = 0;
    FakeCamera()
    {
        state.auto_exposure = true;
        state.sensor_width = 2000;
        state.sensor_height = 1200;
        state.statistics = {0, 0, 2000, 1200};
        state.reported_exposure_us = 12000;
    }
    common::Error ReadState(camera::State *out)
    {
        *out = state;
        return {};
    }
    common::Error GetGeometry(camera::Geometry *out)
    {
        *out = geometry;
        return {};
    }
    common::Error Statistics(camera::Rect rectangle)
    {
        ++writes;
        if (fail)
            return {common::ErrorCode::kHardware};
        state.statistics = rectangle;
        return {};
    }
};

TEST(
    ExposureRuntime,
    RetriesFailedApplyAndRestoresFullFrame
)
{
    exposure_control::Runtime runtime;
    FakeCamera camera;
    runtime.Observe(Face(), 0);
    camera.fail = true;
    EXPECT_FALSE(runtime.Process(camera, 0).Ok());
    EXPECT_NE(runtime.DisplayValues().error_code, 0U);
    camera.fail = false;
    EXPECT_TRUE(runtime.Process(camera, 250).Ok());
    EXPECT_EQ(camera.writes, 2U);
    EXPECT_EQ(runtime.DisplayValues().applied.x, 240U);
    EXPECT_TRUE(runtime.Process(camera, 1000).Ok());
    EXPECT_EQ(camera.state.statistics.width, 2000U);
}

TEST(
    ExposureRuntime,
    RespectsManualExposureAndReappliesAfterRecovery
)
{
    exposure_control::Runtime runtime;
    FakeCamera camera;
    runtime.Observe(Face(), 0);
    camera.state.auto_exposure = false;
    EXPECT_TRUE(runtime.Process(camera, 0).Ok());
    EXPECT_EQ(camera.writes, 0U);
    camera.state.auto_exposure = true;
    EXPECT_TRUE(runtime.Process(camera, 250).Ok());
    EXPECT_EQ(camera.writes, 1U);
    camera.state.statistics = {0, 0, 2000, 1200};
    EXPECT_TRUE(runtime.Process(camera, 500).Ok());
    EXPECT_EQ(camera.writes, 2U);
}
}