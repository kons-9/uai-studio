#ifndef UAI_AI_MIDDLEWARE_MEMORY_STATIC_MEMORY_LAYOUT_HPP
#define UAI_AI_MIDDLEWARE_MEMORY_STATIC_MEMORY_LAYOUT_HPP

/* Public API and value types for the auto_static_memory_layout result. */
#include <array>
#include <cstddef>
#include <cstdint>

namespace uai::ai::static_memory_layout {
enum class Key: uint8_t;

struct AddressRange {
    std::uintptr_t begin = 0U;
    std::uintptr_t end = 0U;

    bool is_valid() const
    {
        return begin != 0U && end >= begin && end > begin;
    }

    bool overlaps(const AddressRange &other) const
    {
        return begin < other.end && other.begin < end;
    }
};

struct Region {
    const std::uint8_t *begin = nullptr;
    const std::uint8_t *end = nullptr;

    std::uintptr_t address() const
    {
        return reinterpret_cast<std::uintptr_t>(begin);
    }

    std::size_t size() const
    {
        return reinterpret_cast<std::uintptr_t>(end) - address();
    }

    AddressRange to_address_range() const
    {
        return {address(), reinterpret_cast<std::uintptr_t>(end)};
    }

    bool is_valid() const
    {
        return to_address_range().is_valid();
    }
    
    static const Region GetRegionFromKey(Key key);
};

template <std::size_t kRegionCount>
struct Layout {
    std::array<Region, kRegionCount> regions{};

    constexpr const Region &Get(std::size_t index) const
    {
        return regions[index];
    }
};

} // namespace uai::ai::static_memory_layout

#endif // UAI_AI_MIDDLEWARE_MEMORY_STATIC_MEMORY_LAYOUT_HPP
