#pragma once

#include <cstddef>
#include <cstdint>

#include "middleware/buffer/owned_buffer.hpp"

namespace uai::ai::inference {

inline constexpr std::size_t kMaxBoxes = 16U;
inline constexpr std::size_t kSegmentationMaskWidth = 20U;
inline constexpr std::size_t kSegmentationMaskHeight = 20U;

struct Box {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    float confidence = 0.0F;
};

struct DetectionSet {
    std::uint32_t count = 0U;
    Box boxes[kMaxBoxes]{};
};

struct SegmentationSet {
    common::OwnedBuffer<std::uint8_t, kSegmentationMaskWidth * kSegmentationMaskHeight> mask{};
    std::uint16_t mask_width = 0U;
    std::uint16_t mask_height = 0U;
    std::uint32_t mask_foreground_pixels = 0U;
};

struct BoxSet {
    std::uint32_t model_sequence = 0U;
    std::uint32_t capture_sequence = 0U;
    bool person_valid = false;
    bool face_valid = false;
    bool segmentation_valid = false;
    DetectionSet person{};
    DetectionSet face{};
    SegmentationSet segmentation{};
};

} // namespace uai::ai::inference
