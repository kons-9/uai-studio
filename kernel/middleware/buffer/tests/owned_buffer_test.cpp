#include "middleware/buffer/owned_buffer.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace uai::ai::common {
namespace {

TEST(OwnedBufferTest, CapacityCountsElements)
{
    OwnedBuffer<std::uint16_t, 2U> values{};
    const std::uint16_t source[]{100U, 200U, 300U};
    ASSERT_TRUE(values.CopyFrom(source, 2U).Ok());
    EXPECT_EQ(values.data()[1], 200U);
    EXPECT_EQ(values.CopyFrom(source, 3U).Code(), ErrorCode::kBufferOverflow);
    EXPECT_EQ(values.data()[1], 200U);
}

} // namespace
} // namespace uai::ai::common