#include "middleware/message_channel/message_channel.hpp"
#include "middleware/message_channel/utkernel_backend.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

struct Payload {
    std::uint8_t bytes[5];
};

constexpr ER kOtherError = -17;  // any non-E_OK, non-E_TMOUT code
T_CMBF created{};
ID next_id = 1;
Payload queued{};
bool pending = false;
TMO send_timeout = TMO_FEVR;
TMO receive_timeout = TMO_FEVR;
ER send_result = E_OK;
INT receive_result = 0;

} // namespace

ID tk_cre_mbf(const T_CMBF *config)
{
    created = *config;
    return next_id;
}

ER tk_snd_mbf(ID queue, const void *message, SZ size, TMO timeout)
{
    if (queue != 1 || size != sizeof(Payload)) return kOtherError;
    send_timeout = timeout;
    if (send_result != E_OK) return send_result;
    std::memcpy(&queued, message, size);
    pending = true;
    return E_OK;
}

INT tk_rcv_mbf(ID queue, void *message, TMO timeout)
{
    if (queue != 1) return kOtherError;
    receive_timeout = timeout;
    if (receive_result != 0) return receive_result;
    if (!pending) return E_TMOUT;
    std::memcpy(message, &queued, sizeof(queued));
    pending = false;
    return sizeof(queued);
}

namespace uai::ai::message_channel {
namespace {

using Channel = MessageChannel<Payload, 3U, MicroTKernelBackend<Payload, 3U>>;

class MessageChannelTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ::created = {};
        ::next_id = 1;
        ::pending = false;
        ::send_result = E_OK;
        ::receive_result = 0;
    }
};

TEST_F(MessageChannelTest, BackendOwnsUserBufferAndMapsTimeouts)
{
    static_assert(!std::is_copy_constructible_v<Channel>);
    static_assert(!std::is_move_constructible_v<Channel>);
    Channel channel;
    EXPECT_FALSE(channel.created());
    ASSERT_TRUE(channel.Create().Ok());
    EXPECT_TRUE(channel.created());
    EXPECT_EQ(::created.mbfatr, TA_TFIFO | TA_USERBUF);
    EXPECT_EQ(::created.maxmsz, sizeof(Payload));
    EXPECT_EQ(::created.bufsz, 3U * (sizeof(INT) + 8U));
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(::created.bufptr) % 8U, 0U);

    const Payload sent{{1U, 2U, 3U, 4U, 5U}};
    Payload received{};
    EXPECT_TRUE(channel.TrySend(sent).Ok());
    EXPECT_EQ(::send_timeout, TMO_POL);
    EXPECT_TRUE(channel.Receive(&received).Ok());
    EXPECT_EQ(::receive_timeout, TMO_FEVR);
    EXPECT_EQ(std::memcmp(&sent, &received, sizeof(sent)), 0);

    EXPECT_TRUE(channel.Send(sent).Ok());
    EXPECT_EQ(::send_timeout, TMO_FEVR);
    EXPECT_TRUE(channel.TryReceive(&received).Ok());
    EXPECT_EQ(::receive_timeout, TMO_POL);
}

TEST_F(MessageChannelTest, MapsOsResultsToErrorCodes)
{
    Channel channel;
    Payload payload{};
    EXPECT_EQ(channel.TrySend(payload).Code(), common::ErrorCode::kNotInitialized);
    EXPECT_EQ(channel.TryReceive(&payload).Code(), common::ErrorCode::kNotInitialized);

    ::next_id = kOtherError;
    EXPECT_EQ(channel.Create().Code(), common::ErrorCode::kHardware);
    EXPECT_FALSE(channel.created());
    ::next_id = 1;
    ASSERT_TRUE(channel.Create().Ok());

    EXPECT_EQ(channel.TryReceive(&payload).Code(), common::ErrorCode::kNoFrame);
    EXPECT_EQ(channel.TryReceive(nullptr).Code(), common::ErrorCode::kInvalidArgument);

    ::send_result = E_TMOUT;
    EXPECT_EQ(channel.TrySend(payload).Code(), common::ErrorCode::kBufferOverflow);
    ::send_result = kOtherError;
    EXPECT_EQ(channel.TrySend(payload).Code(), common::ErrorCode::kHardware);

    ::receive_result = kOtherError;
    EXPECT_EQ(channel.TryReceive(&payload).Code(), common::ErrorCode::kHardware);
    ::receive_result = static_cast<INT>(sizeof(Payload) - 1U);
    EXPECT_EQ(channel.TryReceive(&payload).Code(), common::ErrorCode::kHardware);
}

} // namespace
} // namespace uai::ai::message_channel
