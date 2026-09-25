#ifndef UAI_CAMERA_LCD_FRAME_BUFFER_HPP
#define UAI_CAMERA_LCD_FRAME_BUFFER_HPP

#include <cstddef>
#include <cstdint>

namespace uai::camera_lcd::driver {

constexpr std::size_t kFrameWidth = 800U;
constexpr std::size_t kFrameHeight = 480U;
constexpr std::size_t kFrameBytes = kFrameWidth * kFrameHeight * 2U;

std::uint8_t *FrameBuffer();
std::uintptr_t FrameBufferAddress();

} // namespace uai::camera_lcd::driver

#endif
