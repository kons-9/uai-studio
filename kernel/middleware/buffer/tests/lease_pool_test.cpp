#include "middleware/buffer/lease_pool.hpp"

#include <gtest/gtest.h>

namespace uai::ai::buffer {
namespace {

TEST(
    LeasePoolTest,
    TokensDistinguishLeasesOfTheSameSlot
)
{
    LeasePool<2U> pool;
    pool[0].buffer = {0x1000U, 64U, 0U, Region::kDisplay};
    pool[1].buffer = {0x2000U, 64U, 1U, Region::kDisplay};
    EXPECT_EQ(pool.Find(2U), nullptr);
    EXPECT_EQ(pool.FindByAddress(0x3000U), nullptr);
    EXPECT_EQ(pool.FindByAddress(0x2000U), &pool[1]);

    auto *slot = pool.FindFree();
    ASSERT_EQ(slot, &pool[0]);
    const std::uint64_t first = pool.Lease(*slot, BufferState::kFilling);
    EXPECT_NE(first, 0U);
    EXPECT_EQ(slot->state, BufferState::kFilling);
    EXPECT_EQ(slot->lease_token, first);
    EXPECT_EQ(pool.FindFree(), &pool[1]);

    pool.Release(*slot);
    EXPECT_EQ(slot->state, BufferState::kFree);
    EXPECT_EQ(slot->lease_token, 0U);

    const std::uint64_t second = pool.Lease(*slot, BufferState::kReadyForAi);
    EXPECT_NE(second, first);
    pool.Lease(pool[1], BufferState::kFilling);
    EXPECT_EQ(pool.FindFree(), nullptr);
}

} // namespace
} // namespace uai::ai::buffer
