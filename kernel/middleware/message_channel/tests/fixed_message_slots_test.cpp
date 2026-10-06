#include "middleware/message_channel/fixed_message_slots.hpp"

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace uai::ai::message_channel {
namespace {

struct OddMessage {
    std::uint8_t bytes[5];
};

TEST(FixedMessageSlotsTest, ReservesHeaderAndRoundedPayloadForEachMessage)
{
    FixedMessageSlots<OddMessage, 3U> storage;
    static_assert(FixedMessageSlots<OddMessage, 3U>::message_bytes() == 5U);
    static_assert(FixedMessageSlots<OddMessage, 3U>::size_bytes() == 36U);
    static_assert(alignof(FixedMessageSlots<OddMessage, 3U>) >= 8U);
    static_assert(!std::is_copy_constructible_v<
                  FixedMessageSlots<OddMessage, 3U>>);
    static_assert(!std::is_move_constructible_v<
                  FixedMessageSlots<OddMessage, 3U>>);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(storage.data()) % 8U, 0U);
    EXPECT_EQ(storage.size_bytes(), 3U * (4U + 8U));
}

} // namespace
} // namespace uai::ai::message_channel