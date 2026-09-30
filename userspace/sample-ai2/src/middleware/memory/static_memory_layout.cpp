#include "middleware/memory/static_memory_layout.hpp"

#include "middleware/memory/generated/static_memory_layout/raw.hpp"

namespace uai::ai::static_memory_layout {

const Region Region::GetRegionFromKey(Key key)
{
    return kLayout.Get(static_cast<std::size_t>(key));
}

} // namespace uai::ai::static_memory_layout
