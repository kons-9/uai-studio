#pragma once

#include <array>
#include "middleware/memory/static_memory_layout.hpp"

namespace uai::ai::static_memory_layout {

enum class Key : std::uint8_t {
    kCapture0, kCapture1, kDisplay0, kDisplay1,
    kInference0, kInference1, kInference2,
    kInferenceSource0, kInferenceSource1, kInferenceSource2,
    kInferenceScratch, kPipe2Drop, kCount,
};

inline constexpr std::array<Key, 3U> kInferenceRegionKeys{
    Key::kInference0, Key::kInference1, Key::kInference2};
inline constexpr std::array<Key, 3U> kInferenceSourceRegionKeys{
    Key::kInferenceSource0, Key::kInferenceSource1, Key::kInferenceSource2};

} // namespace uai::ai::static_memory_layout
