#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::buffer_layout {

/* Half-open ranges. Arithmetic avoids address + size overflow, so the same
 * validation works on the host and on a 32-bit target. */
struct Range {
    std::uintptr_t start = 0U;
    std::size_t size = 0U;
};

constexpr bool Contains(Range outer, Range inner)
{
    return inner.start >= outer.start &&
           inner.start - outer.start <= outer.size &&
           inner.size <= outer.size - (inner.start - outer.start);
}

constexpr bool Overlaps(Range lhs, Range rhs)
{
    return lhs.size != 0U && rhs.size != 0U &&
           ((lhs.start <= rhs.start && Contains(lhs, {rhs.start, 1U})) ||
            (rhs.start <= lhs.start && Contains(rhs, {lhs.start, 1U})));
}

/* Verify actual allocations rather than only checking the linker reservations.
 * Unused tail bytes of each slot are kept out of the live allocation. */
inline bool Disjoint(const Range *ranges, std::size_t count)
{
    if (ranges == nullptr) {
        return false;
    }
    for (std::size_t i = 0U; i < count; ++i) {
        if (ranges[i].start == 0U || ranges[i].size == 0U) {
            return false;
        }
        for (std::size_t j = 0U; j < i; ++j) {
            if (Overlaps(ranges[i], ranges[j])) {
                return false;
            }
        }
    }
    return true;
}

/* CPU-visible sentinel is outside the live allocation, on its own cache
 * line. Caller handles platform-specific cache invalidation before reading. */
constexpr std::size_t kGuardBytes = 32U;
constexpr std::uint32_t kGuardWord = 0xC39A6D52U;

inline void ArmGuard(std::uintptr_t address)
{
    auto *words = reinterpret_cast<volatile std::uint32_t *>(address);
    for (std::size_t i = 0U; i < kGuardBytes / sizeof(std::uint32_t); ++i) {
        words[i] = kGuardWord;
    }
}

inline bool GuardIntact(std::uintptr_t address)
{
    const auto *words = reinterpret_cast<volatile const std::uint32_t *>(address);
    for (std::size_t i = 0U; i < kGuardBytes / sizeof(std::uint32_t); ++i) {
        if (words[i] != kGuardWord) {
            return false;
        }
    }
    return true;
}

} // namespace uai::ai::buffer_layout
