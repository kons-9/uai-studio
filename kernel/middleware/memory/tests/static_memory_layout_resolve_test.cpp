#include "middleware/memory/generated/static_memory_layout/key.hpp"
#include "middleware/memory/static_memory_layout.hpp"

#include <gtest/gtest.h>

extern "C" {
std::uint8_t g_test_first_region[32U];
std::uint8_t g_test_second_region[64U];
std::uint8_t g_test_third_region[16U];
}

namespace uai::ai::static_memory_layout {
namespace {

TEST(StaticMemoryLayoutResolve, KeyIndexesGeneratedLayout)
{
    const Region first = Region::GetRegionFromKey(Key::kFirst);
    const Region second = Region::GetRegionFromKey(Key::kSecond);
    const Region third = Region::GetRegionFromKey(Key::kThird);
    EXPECT_TRUE(first.is_valid());
    EXPECT_TRUE(second.is_valid());
    EXPECT_TRUE(third.is_valid());
    EXPECT_EQ(first.address(), reinterpret_cast<std::uintptr_t>(g_test_first_region));
    EXPECT_EQ(first.size(), sizeof(g_test_first_region));
    EXPECT_EQ(second.address(), reinterpret_cast<std::uintptr_t>(g_test_second_region));
    EXPECT_EQ(second.size(), sizeof(g_test_second_region));
    EXPECT_EQ(third.address(), reinterpret_cast<std::uintptr_t>(g_test_third_region));
    EXPECT_EQ(third.size(), sizeof(g_test_third_region));
    EXPECT_FALSE(first.to_address_range().overlaps(second.to_address_range()));
    EXPECT_FALSE(second.to_address_range().overlaps(third.to_address_range()));
}

} // namespace
} // namespace uai::ai::static_memory_layout
