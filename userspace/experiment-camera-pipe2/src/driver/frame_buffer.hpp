#ifndef UAI_CAMERA_PIPE2_FRAME_BUFFER_HPP
#define UAI_CAMERA_PIPE2_FRAME_BUFFER_HPP

#include <cstddef>
#include <cstdint>

namespace uai::camera_pipe2::driver {

#ifndef PIPE2_PIPE_DUAL
#define PIPE2_PIPE_DUAL 1
#endif

#if PIPE2_PIPE_DUAL
/* Baseline: Pipe1 and Pipe2 are shown side by side on the 800x480 LCD. */
constexpr std::size_t kPipeCount = 2U;
constexpr std::size_t kFrameWidth = 400U;
#else
/* Comparison: Pipe1 alone fills the 800x480 LCD. */
constexpr std::size_t kPipeCount = 1U;
constexpr std::size_t kFrameWidth = 800U;
#endif
constexpr std::size_t kFrameHeight = 480U;
constexpr std::size_t kFrameBytesPerLine = kFrameWidth * 2U;
constexpr std::size_t kDisplayWidth = kFrameWidth * kPipeCount;
constexpr std::size_t kDisplayBytesPerLine = kDisplayWidth * 2U;
constexpr std::size_t kDisplayFrameBytes = kDisplayBytesPerLine * kFrameHeight;
constexpr std::size_t kFrameBytes = kFrameWidth * kFrameHeight * 2U;

std::uint8_t *MainPipeFrameBuffer();
std::uint8_t *AncillaryPipeFrameBuffer();
std::uintptr_t MainPipeFrameBufferAddress();
std::uintptr_t AncillaryPipeFrameBufferAddress();

} // namespace uai::camera_pipe2::driver

#endif
