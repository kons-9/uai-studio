#include "driver/frame_buffer.hpp"

namespace uai::camera_pipe2::driver {

namespace {

alignas(64) std::uint8_t g_main_pipe_frame_buffer[kFrameBytes]
    __attribute__((section(".camera_frame_buffer"), used));
alignas(64) std::uint8_t g_ancillary_pipe_frame_buffer[kFrameBytes]
    __attribute__((section(".camera_frame_buffer"), used));

} // namespace

std::uint8_t *MainPipeFrameBuffer()
{
    return g_main_pipe_frame_buffer;
}

std::uint8_t *AncillaryPipeFrameBuffer()
{
    return g_ancillary_pipe_frame_buffer;
}

std::uintptr_t MainPipeFrameBufferAddress()
{
    return reinterpret_cast<std::uintptr_t>(g_main_pipe_frame_buffer);
}

std::uintptr_t AncillaryPipeFrameBufferAddress()
{
    return reinterpret_cast<std::uintptr_t>(g_ancillary_pipe_frame_buffer);
}

} // namespace uai::camera_pipe2::driver
