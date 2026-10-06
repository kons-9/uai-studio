#include "middleware/foundation/message_channel.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

struct Payload {
    std::uint8_t bytes[5];
};

T_CMBF created{};
Payload queued{};
bool pending = false;
TMO send_timeout = TMO_FEVR;
TMO receive_timeout = TMO_FEVR;
ER send_result = E_OK;

} // namespace

ID tk_cre_mbf(const T_CMBF *config)
{
    created = *config;
    return 1;
}

ER tk_snd_mbf(ID queue, const void *message, SZ size, TMO timeout)
{
    if (queue != 1 || size != sizeof(Payload)) return E_TMOUT;
    send_timeout = timeout;
    if (send_result != E_OK) return send_result;
    std::memcpy(&queued, message, size);
    pending = true;
    return E_OK;
}

INT tk_rcv_mbf(ID queue, void *message, TMO timeout)
{
    if (queue != 1) return E_TMOUT;
    receive_timeout = timeout;
    if (!pending) return E_TMOUT;
    std::memcpy(message, &queued, sizeof(queued));
    pending = false;
    return sizeof(queued);
}

namespace uai::ai::common {
namespace {

TEST(MessageChannelTest, OwnsUserBufferAndForwardsTypedMessages)
{
    ::pending = false;
    ::send_result = E_OK;
    MessageChannel<Payload, 3U> channel;
    static_assert(!std::is_copy_constructible_v<MessageChannel<Payload, 3U>>);
    static_assert(!std::is_move_constructible_v<MessageChannel<Payload, 3U>>);
    EXPECT_EQ(channel.Create(), 1);
    EXPECT_EQ(::created.mbfatr, TA_TFIFO | TA_USERBUF);
    EXPECT_EQ(::created.maxmsz, sizeof(Payload));
    EXPECT_EQ(::created.bufsz, 3U * (sizeof(INT) + 8U));
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(::created.bufptr) % 8U, 0U);

    const Payload sent{{1U, 2U, 3U, 4U, 5U}};
    Payload received{};
    EXPECT_EQ(channel.Send(sent, TMO_POL), E_OK);
    EXPECT_EQ(::send_timeout, TMO_POL);
    EXPECT_EQ(channel.Receive(&received, TMO_FEVR), sizeof(Payload));
    EXPECT_EQ(::receive_timeout, TMO_FEVR);
    EXPECT_EQ(std::memcmp(&sent, &received, sizeof(sent)), 0);

    ::send_result = E_TMOUT;
    EXPECT_EQ(channel.Send(sent, TMO_POL), E_TMOUT);
    EXPECT_EQ(channel.Receive(&received, TMO_POL), E_TMOUT);
}

} // namespace
} // namespace uai::ai::common