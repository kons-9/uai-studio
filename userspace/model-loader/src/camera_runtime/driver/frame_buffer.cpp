#include "driver/frame_buffer.hpp"

namespace uai::camera_pipe2::driver {

namespace {

#ifndef PIPE2_BUFFER_PSRAM
#define PIPE2_BUFFER_PSRAM 0
#endif

#if PIPE2_BUFFER_PSRAM
/* main() initializes APS256XX before either capture buffer is accessed. */
constexpr std::uintptr_t kMainPsramAddress = 0x91000000U;
constexpr std::uintptr_t kAncillaryPsramAddress = 0x91100000U;
#else
alignas(64) std::uint8_t g_main_pipe_frame_buffer[kFrameBytes] __attribute__((
    section(".camera_frame_buffer"),
    used
));
#if PIPE2_PIPE_DUAL
alignas(64) std::uint8_t g_ancillary_pipe_frame_buffer[kFrameBytes] __attribute__((
    section(".camera_frame_buffer"),
    used
));
#endif
#endif

} // namespace

std::uint8_t *MainPipeFrameBuffer()
{
#if PIPE2_BUFFER_PSRAM
    return reinterpret_cast<std::uint8_t *>(kMainPsramAddress);
#else
    return g_main_pipe_frame_buffer;
#endif
}

std::uint8_t *AncillaryPipeFrameBuffer()
{
#if !PIPE2_PIPE_DUAL
    return nullptr;
#elif PIPE2_BUFFER_PSRAM
    return reinterpret_cast<std::uint8_t *>(kAncillaryPsramAddress);
#else
    return g_ancillary_pipe_frame_buffer;
#endif
}

std::uintptr_t MainPipeFrameBufferAddress()
{
#if PIPE2_BUFFER_PSRAM
    return kMainPsramAddress;
#else
    return reinterpret_cast<std::uintptr_t>(g_main_pipe_frame_buffer);
#endif
}

std::uintptr_t AncillaryPipeFrameBufferAddress()
{
#if !PIPE2_PIPE_DUAL
    return 0U;
#elif PIPE2_BUFFER_PSRAM
    return kAncillaryPsramAddress;
#else
    return reinterpret_cast<std::uintptr_t>(g_ancillary_pipe_frame_buffer);
#endif
}

} // namespace uai::camera_pipe2::driver
