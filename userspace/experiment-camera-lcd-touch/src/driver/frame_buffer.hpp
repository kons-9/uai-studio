#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::camera_lcd_touch::driver {

constexpr std::size_t kFrameWidth = 800U;
constexpr std::size_t kFrameHeight = 480U;
constexpr std::size_t kFrameBytes = kFrameWidth * kFrameHeight * 2U;

std::uint8_t *FrameBuffer();
std::uintptr_t FrameBufferAddress();
std::uint16_t *DisplayFrameBuffer(std::size_t index);
std::uintptr_t DisplayFrameBufferAddress(std::size_t index);

} // namespace uai::camera_lcd_touch::driver
