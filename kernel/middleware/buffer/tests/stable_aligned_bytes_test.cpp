#include "middleware/buffer/stable_aligned_bytes.hpp"

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace uai::ai::common {
namespace {

TEST(StableAlignedBytesTest, HasAlignedUserBufferAndExactByteSize)
{
    StableAlignedBytes<16U> bytes;
    static_assert(alignof(StableAlignedBytes<16U>) >= 8U);
    static_assert(sizeof(StableAlignedBytes<16U>) == 16U);
    static_assert(!std::is_copy_constructible_v<StableAlignedBytes<16U>>);
    static_assert(!std::is_move_constructible_v<StableAlignedBytes<16U>>);
    EXPECT_EQ(bytes.size_bytes(), 16U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(bytes.data()) % 8U, 0U);

    auto *data = static_cast<std::byte *>(bytes.data());
    data[0] = std::byte{1U};
    data[bytes.size_bytes() - 1U] = std::byte{2U};
    EXPECT_EQ(data[0], std::byte{1U});
    EXPECT_EQ(data[bytes.size_bytes() - 1U], std::byte{2U});
}

} // namespace
} // namespace uai::ai::common