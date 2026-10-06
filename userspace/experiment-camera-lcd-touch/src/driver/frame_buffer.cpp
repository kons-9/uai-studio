#include "driver/frame_buffer.hpp"

namespace uai::camera_lcd_touch::driver {

namespace {

alignas(64) std::uint8_t g_frame_buffer[kFrameBytes]
    __attribute__((section(".camera_frame_buffer"), used));
alignas(64) std::uint16_t g_display_frame_buffer_0[kFrameWidth * kFrameHeight]
    __attribute__((section(".display_frame_buffer_0"), used));
alignas(64) std::uint16_t g_display_frame_buffer_1[kFrameWidth * kFrameHeight]
    __attribute__((section(".display_frame_buffer_1"), used));

} // namespace

std::uint8_t *FrameBuffer()
{
    return g_frame_buffer;
}

std::uintptr_t FrameBufferAddress()
{
    return reinterpret_cast<std::uintptr_t>(g_frame_buffer);
}

std::uint16_t *DisplayFrameBuffer(std::size_t index)
{
    switch (index) {
    case 0U: return g_display_frame_buffer_0;
    case 1U: return g_display_frame_buffer_1;
    default: return nullptr;
    }
}

std::uintptr_t DisplayFrameBufferAddress(std::size_t index)
{
    return reinterpret_cast<std::uintptr_t>(DisplayFrameBuffer(index));
}

} // namespace uai::camera_lcd_touch::driver
