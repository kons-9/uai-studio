/* Host stand-in for the generated raw.hpp. The real file binds Region begin/end
 * to linker section symbols; here they point into arrays owned by the test so
 * the same static_memory_layout.cpp can be linked and exercised. */
#pragma once

#include <cstdint>

#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"

extern "C" {
extern std::uint8_t g_test_first_region[32U];
extern std::uint8_t g_test_second_region[64U];
extern std::uint8_t g_test_third_region[16U];
}

namespace uai::ai::static_memory_layout {

inline const Layout<static_cast<std::size_t>(Key::kCount)> kLayout = {
    {{
        {g_test_first_region, g_test_first_region + sizeof(g_test_first_region)},
        {g_test_second_region, g_test_second_region + sizeof(g_test_second_region)},
        {g_test_third_region, g_test_third_region + sizeof(g_test_third_region)},
    }},
};

} // namespace uai::ai::static_memory_layout
