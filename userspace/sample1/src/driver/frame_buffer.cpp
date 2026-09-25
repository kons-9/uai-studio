#include "driver/frame_buffer.hpp"

namespace uai::sample1::driver {

namespace {

alignas(64) std::uint8_t g_frame_buffer[kFrameBytes]
    __attribute__((section(".camera_frame_buffer"), used));

} // namespace

std::uint8_t *FrameBuffer()
{
    return g_frame_buffer;
}

std::uintptr_t FrameBufferAddress()
{
    return reinterpret_cast<std::uintptr_t>(g_frame_buffer);
}

} // namespace uai::sample1::driver
