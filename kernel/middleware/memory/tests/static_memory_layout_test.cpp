#include "middleware/memory/static_memory_layout.hpp"

#include <gtest/gtest.h>

namespace uai::ai::static_memory_layout {
namespace {

TEST(StaticMemoryLayout, AddressRangeValidityAndOverlap)
{
    EXPECT_TRUE((AddressRange{0x1000U, 0x2000U}).is_valid());
    EXPECT_FALSE((AddressRange{0x1000U, 0x1000U}).is_valid());
    EXPECT_FALSE((AddressRange{0x2000U, 0x1000U}).is_valid());
    EXPECT_FALSE((AddressRange{0U, 0x1000U}).is_valid());
    EXPECT_FALSE(AddressRange{}.is_valid());

    const AddressRange base{0x1000U, 0x2000U};
    EXPECT_FALSE(base.overlaps({0x2000U, 0x3000U}));
    EXPECT_FALSE(base.overlaps({0x0000U, 0x1000U}));
    EXPECT_TRUE(base.overlaps({0x1FFFU, 0x3000U}));
    EXPECT_TRUE(base.overlaps({0x1400U, 0x1800U}));
    EXPECT_TRUE(base.overlaps({0x0800U, 0x2800U}));
    EXPECT_TRUE(base.overlaps(base));
}

TEST(StaticMemoryLayout, RegionDerivesAddressSizeAndRange)
{
    alignas(32) static std::uint8_t storage[64U];
    const Region region{storage, storage + sizeof(storage)};
    EXPECT_TRUE(region.is_valid());
    EXPECT_EQ(region.address(), reinterpret_cast<std::uintptr_t>(storage));
    EXPECT_EQ(region.size(), sizeof(storage));
    EXPECT_EQ(region.to_address_range().begin, region.address());
    EXPECT_EQ(region.to_address_range().end, region.address() + sizeof(storage));
    EXPECT_FALSE(Region{}.is_valid());
    EXPECT_FALSE((Region{storage, storage}).is_valid());

    constexpr Layout<2U> layout{{{Region{}, Region{}}}};
    EXPECT_EQ(&layout.Get(1U), &layout.regions[1]);
}

} // namespace
} // namespace uai::ai::static_memory_layout
