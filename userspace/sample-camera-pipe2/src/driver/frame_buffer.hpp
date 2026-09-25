#ifndef UAI_CAMERA_PIPE2_FRAME_BUFFER_HPP
#define UAI_CAMERA_PIPE2_FRAME_BUFFER_HPP

#include <cstddef>
#include <cstdint>

namespace uai::camera_pipe2::driver {

/* Pipe1 and Pipe2 are shown side by side on the 800x480 LCD. */
constexpr std::size_t kPipeCount = 2U;
constexpr std::size_t kFrameWidth = 400U;
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
