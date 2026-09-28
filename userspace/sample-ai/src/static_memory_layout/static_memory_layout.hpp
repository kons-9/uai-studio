#ifndef UAI_AI_STATIC_MEMORY_LAYOUT_HPP
#define UAI_AI_STATIC_MEMORY_LAYOUT_HPP

#include <array>
#include <cstddef>
#include <cstdint>

extern "C" {
extern std::uint8_t __sample_ai_capture0_start__[];
extern std::uint8_t __sample_ai_capture0_end__[];
extern std::uint8_t __sample_ai_capture1_start__[];
extern std::uint8_t __sample_ai_capture1_end__[];
extern std::uint8_t __sample_ai_display0_start__[];
extern std::uint8_t __sample_ai_display0_end__[];
extern std::uint8_t __sample_ai_display1_start__[];
extern std::uint8_t __sample_ai_display1_end__[];
extern std::uint8_t __sample_ai_inference0_start__[];
extern std::uint8_t __sample_ai_inference0_end__[];
extern std::uint8_t __sample_ai_inference1_start__[];
extern std::uint8_t __sample_ai_inference1_end__[];
extern std::uint8_t __sample_ai_inference_scratch_start__[];
extern std::uint8_t __sample_ai_inference_scratch_end__[];
extern std::uint8_t __sample_ai_raw_dump_start__[];
extern std::uint8_t __sample_ai_raw_dump_end__[];
extern std::uint8_t __sample_ai_segmentation_mask0_start__[];
extern std::uint8_t __sample_ai_segmentation_mask0_end__[];
extern std::uint8_t __sample_ai_segmentation_mask1_start__[];
extern std::uint8_t __sample_ai_segmentation_mask1_end__[];
}

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

/* Addresses and capacities of memory which is reserved by the linker script.
 * This is deliberately separate from MemoryAllocatorConfig: changing the
 * allocator's sizing policy must not silently change the physical memory
 * map. */
struct Layout {
    std::array<Region, 2U> capture{};
    std::array<Region, 2U> display{};
    std::array<Region, 2U> inference{};
    Region inference_scratch{};
    Region raw_dump{};
    std::array<Region, 2U> segmentation_mask{};
};

inline constexpr Layout kLayout = {
    {{
        {__sample_ai_capture0_start__, __sample_ai_capture0_end__},
        {__sample_ai_capture1_start__, __sample_ai_capture1_end__},
    }},
    {{
        {__sample_ai_display0_start__, __sample_ai_display0_end__},
        {__sample_ai_display1_start__, __sample_ai_display1_end__},
    }},
    {{
        {__sample_ai_inference0_start__, __sample_ai_inference0_end__},
        {__sample_ai_inference1_start__, __sample_ai_inference1_end__},
    }},
    {__sample_ai_inference_scratch_start__,
     __sample_ai_inference_scratch_end__},
    {__sample_ai_raw_dump_start__, __sample_ai_raw_dump_end__},
    {{
        {__sample_ai_segmentation_mask0_start__,
         __sample_ai_segmentation_mask0_end__},
        {__sample_ai_segmentation_mask1_start__,
         __sample_ai_segmentation_mask1_end__},
    }},
};

} // namespace uai::ai::static_memory_layout

#endif
