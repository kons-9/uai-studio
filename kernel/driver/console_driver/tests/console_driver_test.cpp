#include "driver/console_driver/console_driver.hpp"
#include "driver/console_driver/rx_queue.hpp"
#include <gtest/gtest.h>
#include <string>

namespace {
namespace console = uai::ai::console;
using Code = uai::ai::common::ErrorCode;
Code initialize_status = Code::kHardware;
console::RxQueue received;
console::Notifier notification;
std::string written;
TEST(
    ConsoleDriver,
    GuardsLifecycleArgumentsAndOwnership
)
{
    auto &management = console::ConsoleManagement::Instance();
    console::Input input;
    EXPECT_EQ(management.Read(&input).Code(), Code::kNotInitialized);
    EXPECT_EQ(management.Initialize().Code(), Code::kHardware);
    initialize_status = Code::kOk;
    unsigned wake_count = 0;
    ASSERT_TRUE(management
                    .Initialize(
                        {&wake_count,
                         [](void *context) {
                             ++*static_cast<unsigned *>(context);
                         }}
                    )
                    .Ok());
    notification.wake(notification.context);
    EXPECT_EQ(wake_count, 1U);
    EXPECT_EQ(management.Initialize().Code(), Code::kAlreadyInitialized);
    EXPECT_EQ(management.Read(nullptr).Code(), Code::kInvalidArgument);
    EXPECT_EQ(management.Write(nullptr, 1).Code(), Code::kInvalidArgument);
    EXPECT_TRUE(management.Write(nullptr, 0).Ok());
    ASSERT_TRUE(management.Write("hello", 5).Ok());
    EXPECT_EQ(written, "hello");
    EXPECT_EQ(management.Read(&input).Code(), Code::kNoFrame);
    received.Error();
    ASSERT_TRUE(received.Push('x'));
    ASSERT_TRUE(management.Read(&input).Ok());
    EXPECT_EQ(input.value, 'x');
    EXPECT_TRUE(input.error);
    console::ConsoleManagement::Accessor accessor;
    ASSERT_TRUE(management.Acquire(&accessor).Ok());
    console::ConsoleDriver::Writer invalid_writer;
    EXPECT_FALSE(accessor->Write("bad", 3, invalid_writer).Ok());
    EXPECT_EQ(written, "hello");
}
TEST(
    ConsoleQueue,
    PreservesFifoAcrossWrapAndMarksLossOnNextAcceptedByte
)
{
    console::RxQueue queue;
    for (unsigned index = 0; index < 256; ++index)
        ASSERT_TRUE(queue.Push(static_cast<char>(index % 128)));
    EXPECT_FALSE(queue.Push('z'));
    char value = 0;
    bool error = false;
    ASSERT_TRUE(queue.Pop(value, error));
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(error);
    ASSERT_TRUE(queue.Push('x'));
    for (unsigned index = 1; index < 256; ++index) {
        ASSERT_TRUE(queue.Pop(value, error));
        EXPECT_EQ(value, static_cast<char>(index % 128));
        EXPECT_FALSE(error);
    }
    ASSERT_TRUE(queue.Pop(value, error));
    EXPECT_EQ(value, 'x');
    EXPECT_TRUE(error);
    EXPECT_TRUE(queue.Empty());
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
namespace uai::ai::console::registers {
common::Error ConsoleRegisterLayer::Initialize(Notifier notifier)
{
    notification = notifier;
    return {initialize_status};
}
common::Error ConsoleRegisterLayer::Read(Input &input)
{
    return received.Pop(input.value, input.error) ? common::Error{} : common::Error{Code::kNoFrame};
}
common::Error ConsoleRegisterLayer::Write(
    const char *text,
    std::size_t size
)
{
    if (size != 0)
        written.append(text, size);
    return {};
}
}