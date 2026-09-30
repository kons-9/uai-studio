#ifndef UAI_AI_STATIC_MEMORY_LAYOUT_HPP
#define UAI_AI_STATIC_MEMORY_LAYOUT_HPP

/* Public API for the static memory layout. */
#include <cstddef>
#include <cstdint>

#include "memory_manager/static_memory_layout/raw.hpp"

namespace uai::ai::static_memory_layout {

struct AddressRange {
    std::uintptr_t begin = 0U;
    std::uintptr_t end = 0U;
};

const Region &GetRegion(Key key);
const Region &GetRegionByIndex(std::size_t index);
std::size_t GetRegionCount();
const Region &GetInferenceRegion(std::size_t index);
const Region &GetInferenceSourceRegion(std::size_t index);

AddressRange GetAddressRange(const Region &region);
bool IsValidRegion(const Region &region);
bool RegionsOverlap(const AddressRange &lhs, const AddressRange &rhs);

} // namespace uai::ai::static_memory_layout

#endif
