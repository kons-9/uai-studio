#ifndef UAI_AI_STATIC_MEMORY_LAYOUT_TYPE_HPP
#define UAI_AI_STATIC_MEMORY_LAYOUT_TYPE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace uai::ai::static_memory_layout {

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

#endif // UAI_AI_STATIC_MEMORY_LAYOUT_TYPE_HPP
