#pragma once

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
extern std::uint8_t __sample_ai_inference2_start__[];
extern std::uint8_t __sample_ai_inference2_end__[];
extern std::uint8_t __sample_ai_inference_scratch_start__[];
extern std::uint8_t __sample_ai_inference_scratch_end__[];
extern std::uint8_t __sample_ai_inference_source0_start__[];
extern std::uint8_t __sample_ai_inference_source0_end__[];
extern std::uint8_t __sample_ai_inference_source1_start__[];
extern std::uint8_t __sample_ai_inference_source1_end__[];
extern std::uint8_t __sample_ai_inference_source2_start__[];
extern std::uint8_t __sample_ai_inference_source2_end__[];
extern std::uint8_t __sample_ai_raw_dump_start__[];
extern std::uint8_t __sample_ai_raw_dump_end__[];
extern std::uint8_t __sample_ai_segmentation_mask0_start__[];
extern std::uint8_t __sample_ai_segmentation_mask0_end__[];
extern std::uint8_t __sample_ai_segmentation_mask1_start__[];
extern std::uint8_t __sample_ai_segmentation_mask1_end__[];
extern std::uint8_t __sample_ai_pipe2_drop_start__[];
extern std::uint8_t __sample_ai_pipe2_drop_end__[];
extern std::uint8_t __sample_ai_thread_monitor_start__[];
extern std::uint8_t __sample_ai_thread_monitor_end__[];
}

namespace uai::ai::static_memory_layout {

enum class Key : std::uint8_t {
    kCapture0,
    kCapture1,
    kDisplay0,
    kDisplay1,
    kInference0,
    kInference1,
    kInference2,
    kInferenceScratch,
    kInferenceSource0,
    kInferenceSource1,
    kInferenceSource2,
    kRawDump,
    kSegmentationMask0,
    kSegmentationMask1,
    kPipe2Drop,
    kThreadMonitor,
    kCount,
};

struct Region {
    const std::uint8_t *begin = nullptr;
    const std::uint8_t *end = nullptr;

    std::uintptr_t address() const { return reinterpret_cast<std::uintptr_t>(begin); }

    std::size_t size() const { return reinterpret_cast<std::uintptr_t>(end) - address(); }
};

/* Addresses and capacities of memory which is reserved by the linker script.
 * This is deliberately separate from MemoryAllocatorConfig: changing the
 * allocator's sizing policy must not silently change the physical memory
 * map. */
struct Layout {
    std::array<Region, static_cast<std::size_t>(Key::kCount)> regions{};

    constexpr const Region &Get(Key key) const { return regions[static_cast<std::size_t>(key)]; }
};

inline constexpr Layout kLayout = {
    {{
        {__sample_ai_capture0_start__, __sample_ai_capture0_end__},
        {__sample_ai_capture1_start__, __sample_ai_capture1_end__},
        {__sample_ai_display0_start__, __sample_ai_display0_end__},
        {__sample_ai_display1_start__, __sample_ai_display1_end__},
        {__sample_ai_inference0_start__, __sample_ai_inference0_end__},
        {__sample_ai_inference1_start__, __sample_ai_inference1_end__},
        {__sample_ai_inference2_start__, __sample_ai_inference2_end__},
        {__sample_ai_inference_scratch_start__, __sample_ai_inference_scratch_end__},
        {__sample_ai_inference_source0_start__, __sample_ai_inference_source0_end__},
        {__sample_ai_inference_source1_start__, __sample_ai_inference_source1_end__},
        {__sample_ai_inference_source2_start__, __sample_ai_inference_source2_end__},
        {__sample_ai_raw_dump_start__, __sample_ai_raw_dump_end__},
        {__sample_ai_segmentation_mask0_start__, __sample_ai_segmentation_mask0_end__},
        {__sample_ai_segmentation_mask1_start__, __sample_ai_segmentation_mask1_end__},
        {__sample_ai_pipe2_drop_start__, __sample_ai_pipe2_drop_end__},
        {__sample_ai_thread_monitor_start__, __sample_ai_thread_monitor_end__},
    }},
};

} // namespace uai::ai::static_memory_layout
