#include "driver/lcd_driver/display_driver.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

namespace {
namespace lcd = uai::ai::lcd;
using Code = uai::ai::common::ErrorCode;
using BackendCode = uai::driver::DriverStatus;
struct State {
    BackendCode initialize = BackendCode::kOk;
    BackendCode synchronize = BackendCode::kOk;
    BackendCode present = BackendCode::kOk;
    Code clean = Code::kOk;
    ER lock = E_OK;
    std::uintptr_t address = 0;
    std::vector<std::string> events;
} state;
uai::ai::buffer::Buffer Frame()
{
    return {0x91010000U, lcd::kDisplayBytes, 0U, uai::ai::buffer::Region::kDisplay};
}
TEST(
    DisplayDriver,
    ValidatesLifecycleOwnershipCacheAndVblankHandoff
)
{
    auto &display = lcd::DisplayManagement::Instance();
    EXPECT_EQ(display.Present(Frame()).Code(), Code::kNotInitialized);
    auto invalid = Frame();
    invalid.size -= 1;
    EXPECT_EQ(display.Initialize(invalid).Code(), Code::kInvalidArgument);
    invalid = Frame();
    ++invalid.address;
    EXPECT_EQ(display.Initialize(invalid).Code(), Code::kInvalidArgument);
    invalid = Frame();
    invalid.region = uai::ai::buffer::Region::kCapture;
    EXPECT_EQ(display.Initialize(invalid).Code(), Code::kInvalidArgument);
    invalid = Frame();
    invalid.address = std::numeric_limits<std::uintptr_t>::max() & ~std::uintptr_t{31};
    EXPECT_EQ(display.Initialize(invalid).Code(), Code::kInvalidArgument);
    state = {};
    state.clean = Code::kHardware;
    EXPECT_EQ(display.Initialize(Frame()).Code(), Code::kHardware);
    EXPECT_EQ(state.events, (std::vector<std::string>{"clean", "unlock"}));
    state = {};
    state.initialize = BackendCode::kHardwareError;
    EXPECT_EQ(display.Initialize(Frame()).Code(), Code::kHardware);
    state = {};
    ASSERT_TRUE(display.Initialize(Frame()).Ok());
    EXPECT_EQ(state.address, Frame().address);
    EXPECT_EQ(display.Initialize(Frame()).Code(), Code::kAlreadyInitialized);
    state = {};
    invalid = Frame();
    ++invalid.address;
    EXPECT_EQ(display.Present(invalid).Code(), Code::kInvalidArgument);
    EXPECT_EQ(state.events, (std::vector<std::string>{"unlock"}));
    state = {};
    state.synchronize = BackendCode::kBusy;
    EXPECT_EQ(display.Present(Frame()).Code(), Code::kNoBuffer);
    EXPECT_EQ(state.events, (std::vector<std::string>{"sync", "unlock"}));
    EXPECT_EQ(display.Synchronize().Code(), Code::kNoBuffer);
    state = {};
    ASSERT_TRUE(display.Synchronize().Ok());
    state.events.clear();
    ASSERT_TRUE(display.Present(Frame()).Ok());
    EXPECT_EQ(state.events, (std::vector<std::string>{"sync", "clean", "present", "unlock"}));
    state = {};
    state.clean = Code::kHardware;
    EXPECT_EQ(display.Present(Frame()).Code(), Code::kHardware);
    EXPECT_EQ(state.events, (std::vector<std::string>{"sync", "clean", "unlock"}));
    state = {};
    state.present = BackendCode::kHardwareError;
    EXPECT_EQ(display.Present(Frame()).Code(), Code::kHardware);
    state = {};
    ASSERT_TRUE(display.Present(Frame()).Ok());
    lcd::DisplayManagement::Accessor accessor;
    ASSERT_TRUE(display.Acquire(&accessor).Ok());
    lcd::DisplayDriver::Writer invalid_writer;
    EXPECT_FALSE(accessor->Present(Frame(), invalid_writer).Ok());
    accessor = {};
    state.lock = E_TMOUT;
    EXPECT_EQ(display.Acquire(&accessor, 1).Code(), Code::kTimeout);
    EXPECT_EQ(accessor.Get(), nullptr);
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
    return state.lock;
}
ER tk_unl_mtx(ID)
{
    state.events.push_back("unlock");
    return E_OK;
}
namespace uai::ai::cache {
common::Error CacheDriver::Clean(
    void *,
    std::size_t bytes
)
{
    EXPECT_EQ(bytes, lcd::kDisplayBytes);
    state.events.push_back("clean");
    return {state.clean};
}
}
namespace uai::ai::lcd::registers {
uai::driver::DriverStatus LcdRegisterLayer::Initialize(std::uintptr_t initial)
{
    state.events.push_back("init");
    state.address = initial;
    return state.initialize;
}
uai::driver::DriverStatus LcdRegisterLayer::Synchronize()
{
    state.events.push_back("sync");
    return state.synchronize;
}
uai::driver::DriverStatus LcdRegisterLayer::Present(std::uintptr_t address)
{
    state.events.push_back("present");
    state.address = address;
    return state.present;
}
}