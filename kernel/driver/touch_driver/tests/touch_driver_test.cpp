#include "driver/touch_driver/touch_driver.hpp"
#include <gtest/gtest.h>

namespace {
using Code = uai::ai::common::ErrorCode;
uai::ai::ui::TouchPoint controller_sample{true, 900, 600};
Code initialize_status = Code::kHardware;
Code read_status = Code::kOk;
unsigned reads = 0;
TEST(
    TouchDriver,
    PreservesClampedReadAndExposesRawCoordinatesWithOwnership
)
{
    auto &touch = uai::ai::touch::TouchManagement::Instance();
    uai::ai::ui::TouchPoint sample{};
    EXPECT_EQ(touch.ReadRaw(&sample).Code(), Code::kNotInitialized);
    EXPECT_EQ(touch.Initialize().Code(), Code::kHardware);
    initialize_status = Code::kOk;
    ASSERT_TRUE(touch.Initialize().Ok());
    EXPECT_EQ(touch.Initialize().Code(), Code::kAlreadyInitialized);
    EXPECT_EQ(touch.Read(nullptr).Code(), Code::kInvalidArgument);
    ASSERT_TRUE(touch.ReadRaw(&sample).Ok());
    EXPECT_EQ(sample.x, 900);
    EXPECT_EQ(sample.y, 600);
    ASSERT_TRUE(touch.Read(&sample).Ok());
    EXPECT_EQ(sample.x, 799);
    EXPECT_EQ(sample.y, 479);
    controller_sample = {true, 123, 234};
    ASSERT_TRUE(touch.Read(&sample).Ok());
    EXPECT_EQ(sample.x, 123);
    EXPECT_EQ(sample.y, 234);
    read_status = Code::kHardware;
    EXPECT_EQ(touch.ReadRaw(&sample).Code(), Code::kHardware);
    EXPECT_EQ(sample.x, 123);
    uai::ai::touch::TouchManagement::Accessor accessor;
    ASSERT_TRUE(touch.Acquire(&accessor).Ok());
    uai::ai::touch::TouchDriver::Writer invalid_writer;
    const auto baseline = reads;
    EXPECT_FALSE(accessor->ReadRaw(&sample, invalid_writer).Ok());
    EXPECT_EQ(reads, baseline);
}
}
ID tk_cre_mtx(const T_CMTX *)
{
    return 1;
}
ER tk_loc_mtx(
    ID,
    TMO
)
{
    return E_OK;
}
ER tk_unl_mtx(ID)
{
    return E_OK;
}
namespace uai::ai::touch::registers {
common::Error TouchRegisterLayer::Initialize()
{
    return {initialize_status};
}
common::Error TouchRegisterLayer::Read(ui::TouchPoint &sample)
{
    ++reads;
    if (read_status == Code::kOk)
        sample = controller_sample;
    return {read_status};
}
}