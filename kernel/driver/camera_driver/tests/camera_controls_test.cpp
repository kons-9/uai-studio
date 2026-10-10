#include "driver/camera_driver/sensor_driver/registers/isp_controls.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>
extern "C" {
#include "isp_api.h"
ISP_HandleTypeDef hcamera_isp{};
}

namespace {
using Controls = uai::ai::camera::sensor::registers::IspControls;
using Code = uai::ai::common::ErrorCode;
struct Model {
    uai::ai::camera::State value{};
    bool read_failure = false;
    bool gain_failure = false;
    std::vector<std::string> writes;
} model;
ISP_StatusTypeDef GetExposure(
    uint32_t,
    int32_t *value
)
{
    *value = model.value.reported_exposure_us;
    return ISP_OK;
}
ISP_StatusTypeDef GetGain(
    uint32_t,
    int32_t *value
)
{
    *value = model.value.reported_gain_mdB;
    return ISP_OK;
}
ISP_StatusTypeDef SetExposure(
    uint32_t,
    int32_t value
)
{
    model.writes.push_back("exposure");
    model.value.reported_exposure_us = value;
    return ISP_OK;
}
ISP_StatusTypeDef SetGain(
    uint32_t,
    int32_t value
)
{
    model.writes.push_back("gain");
    if (model.gain_failure) {
        model.gain_failure = false;
        return -99;
    }
    model.value.reported_gain_mdB = value;
    return ISP_OK;
}
class CameraControls : public testing::Test {
protected:
    void SetUp() override
    {
        model = {};
        model.value.reported_exposure_us = 1000;
        model.value.reported_gain_mdB = 100;
        model.value.statistics = {0, 0, 2592, 1944};
        hcamera_isp = {};
        hcamera_isp.hDcmipp = &model;
        hcamera_isp.algorithm = &model;
        hcamera_isp.sensorInfo = {2592, 1944, 100, 30000, 0, 24000};
        hcamera_isp.appliHelpers = {GetExposure, GetGain, SetExposure, SetGain};
    }
};
TEST_F(
    CameraControls,
    ReadRejectsMissingHardwareAndDoesNotPublishPartialState
)
{
    uai::ai::camera::State result;
    EXPECT_EQ(Controls::Read(nullptr).Code(), Code::kInvalidArgument);
    hcamera_isp.algorithm = nullptr;
    EXPECT_EQ(Controls::Read(&result).Code(), Code::kNotInitialized);
    hcamera_isp.algorithm = &model;
    result.target = 123;
    model.read_failure = true;
    EXPECT_EQ(Controls::Read(&result).Code(), Code::kHardware);
    EXPECT_EQ(result.target, 123U);
}
TEST_F(
    CameraControls,
    ManualRejectsAutoModeAndSensorLimitsWithoutWrites
)
{
    EXPECT_EQ(Controls::Manual(1000, 100).Code(), Code::kInvalidState);
    ASSERT_TRUE(Controls::AutoExposure(false).Ok());
    model.writes.clear();
    EXPECT_EQ(Controls::Manual(99, 100).Code(), Code::kInvalidArgument);
    EXPECT_EQ(Controls::Manual(30001, 100).Code(), Code::kInvalidArgument);
    EXPECT_EQ(Controls::Manual(1000, -1).Code(), Code::kInvalidArgument);
    EXPECT_EQ(Controls::Manual(1000, 24001).Code(), Code::kInvalidArgument);
    EXPECT_TRUE(model.writes.empty());
}
TEST_F(
    CameraControls,
    ManualFailureRestoresBothSensorValues
)
{
    ASSERT_TRUE(Controls::AutoExposure(false).Ok());
    model.gain_failure = true;
    EXPECT_EQ(Controls::Manual(2000, 200).Code(), Code::kHardware);
    EXPECT_EQ(model.value.reported_exposure_us, 1000);
    EXPECT_EQ(model.value.reported_gain_mdB, 100);
    ASSERT_TRUE(Controls::Manual(2000, 200).Ok());
}
TEST_F(
    CameraControls,
    BoundsCheckingRejectsOverflowAndInvalidCompensation
)
{
    EXPECT_EQ(Controls::Compensation(-5).Code(), Code::kInvalidArgument);
    EXPECT_EQ(Controls::Compensation(5).Code(), Code::kInvalidArgument);
    EXPECT_EQ(Controls::Statistics({2591, 0, 2, 100}).Code(), Code::kInvalidArgument);
    EXPECT_EQ(
        Controls::Statistics({std::numeric_limits<std::uint32_t>::max(), 0, 2, 100}).Code(), Code::kInvalidArgument
    );
    ASSERT_TRUE(Controls::Statistics({20, 30, 100, 200}).Ok());
    EXPECT_EQ(model.value.statistics.x, 20U);
}
TEST_F(
    CameraControls,
    ApplyRestoresPreviousStateWhenWhiteBalanceFails
)
{
    auto requested = model.value;
    requested.compensation = 2;
    requested.auto_white_balance = false;
    requested.color_temperature = 999;
    EXPECT_EQ(Controls::Apply(requested).Code(), Code::kInvalidArgument);
    EXPECT_TRUE(model.value.auto_exposure);
    EXPECT_EQ(model.value.compensation, 0);
    EXPECT_TRUE(model.value.auto_white_balance);
    requested.color_temperature = 4500;
    ASSERT_TRUE(Controls::Apply(requested).Ok());
    EXPECT_EQ(model.value.compensation, 2);
    EXPECT_EQ(model.value.color_temperature, 4500U);
}
TEST_F(
    CameraControls,
    WhiteBalanceListChecksCapacityAndReportsCount
)
{
    std::uint32_t values[3]{};
    std::size_t count = 99;
    EXPECT_EQ(Controls::ListWhiteBalance(values, 2, &count).Code(), Code::kInvalidArgument);
    EXPECT_EQ(count, 99U);
    ASSERT_TRUE(Controls::ListWhiteBalance(values, 3, &count).Ok());
    EXPECT_EQ(count, 3U);
    EXPECT_EQ(values[1], 4500U);
}
TEST(
    CameraConfiguration,
    ValidatesDimensionsFormatsCapacityAndDisjointBuffers
)
{
    uai::ai::camera::CaptureConfiguration configuration;
    configuration.pipe1.buffer = {0x34000000U, 384000};
    configuration.pipe2.buffer = {0x34100000U, 384000};
    EXPECT_TRUE(ValidConfiguration(configuration));
    configuration.pipe2.buffer.size = 383999;
    EXPECT_FALSE(ValidConfiguration(configuration));
    configuration.pipe2.buffer = configuration.pipe1.buffer;
    EXPECT_FALSE(ValidConfiguration(configuration));
    configuration.pipe2.buffer = {0x34100000U, 384000};
    configuration.pipe2.format = static_cast<uai::ai::image_processing::Format>(255);
    EXPECT_FALSE(ValidConfiguration(configuration));
    configuration.pipe2.format = uai::ai::image_processing::Format::kRgb888;
    EXPECT_FALSE(ValidConfiguration(configuration));
    configuration.pipe2.buffer.size = 576000;
    EXPECT_TRUE(ValidConfiguration(configuration));
    configuration.geometry.fps = 11;
    EXPECT_FALSE(ValidConfiguration(configuration));
}
}
extern "C" {
ISP_StatusTypeDef ISP_GetAECState(
    ISP_HandleTypeDef *,
    uint8_t *value
)
{
    *value = model.value.auto_exposure;
    return model.read_failure ? -99 : ISP_OK;
}
ISP_StatusTypeDef ISP_SetAECState(
    ISP_HandleTypeDef *,
    uint8_t value
)
{
    model.writes.push_back("ae");
    model.value.auto_exposure = value != 0;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_GetExposureTarget(
    ISP_HandleTypeDef *,
    ISP_ExposureCompTypeDef *value,
    uint32_t *target
)
{
    *value = model.value.compensation;
    *target = model.value.target;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetExposureTarget(
    ISP_HandleTypeDef *,
    ISP_ExposureCompTypeDef value
)
{
    model.writes.push_back("ev");
    model.value.compensation = value;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_GetStatArea(
    ISP_HandleTypeDef *,
    ISP_StatAreaTypeDef *area
)
{
    const auto &value = model.value.statistics;
    *area = {value.x, value.y, value.width, value.height};
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetStatArea(
    ISP_HandleTypeDef *,
    ISP_StatAreaTypeDef *area
)
{
    model.writes.push_back("area");
    model.value.statistics = {area->X0, area->Y0, area->XSize, area->YSize};
    return ISP_OK;
}
ISP_StatusTypeDef ISP_GetWBRefMode(
    ISP_HandleTypeDef *,
    uint8_t *automatic,
    uint32_t *temperature
)
{
    *automatic = model.value.auto_white_balance;
    *temperature = model.value.color_temperature;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_SetWBRefMode(
    ISP_HandleTypeDef *,
    uint8_t automatic,
    uint32_t temperature
)
{
    model.writes.push_back("wb");
    if (!automatic && temperature != 4500)
        return ISP_ERR_WB_COLORTEMP;
    model.value.auto_white_balance = automatic != 0;
    model.value.color_temperature = temperature;
    return ISP_OK;
}
ISP_StatusTypeDef ISP_ListWBRefModes(
    ISP_HandleTypeDef *,
    uint32_t *values
)
{
    values[0] = 3000;
    values[1] = 4500;
    values[2] = 6500;
    return ISP_OK;
}
}