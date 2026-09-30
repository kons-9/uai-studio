#include "memory_manager/static_memory_layout.hpp"
#include "memory_manager/static_memory_layout/raw.hpp"

namespace uai::ai::static_memory_layout {

const Region &GetRegion(Key key)
{
    return kLayout.Get(static_cast<std::size_t>(key));
}

const Region &GetRegionByIndex(std::size_t index)
{
    return GetRegion(static_cast<Key>(index));
}

std::size_t GetRegionCount()
{
    return static_cast<std::size_t>(Key::kCount);
}

const Region &GetInferenceRegion(std::size_t index)
{
    switch (index) {
    case 0U:
        return GetRegion(Key::kInference0);
    case 1U:
        return GetRegion(Key::kInference1);
    case 2U:
        return GetRegion(Key::kInference2);
    default:
        return GetRegion(Key::kInference0);
    }
}

const Region &GetInferenceSourceRegion(std::size_t index)
{
    switch (index) {
    case 0U:
        return GetRegion(Key::kInferenceSource0);
    case 1U:
        return GetRegion(Key::kInferenceSource1);
    case 2U:
        return GetRegion(Key::kInferenceSource2);
    default:
        return GetRegion(Key::kInferenceSource0);
    }
}

AddressRange GetAddressRange(const Region &region)
{
    return {region.address(), region.address() + region.size()};
}

bool IsValidRegion(const Region &region)
{
    const AddressRange range = GetAddressRange(region);
    return range.begin != 0U && range.end >= range.begin &&
           range.end > range.begin;
}

bool RegionsOverlap(const AddressRange &lhs, const AddressRange &rhs)
{
    return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

} // namespace uai::ai::static_memory_layout
