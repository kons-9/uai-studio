#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::hwtest::camera {

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

} // namespace uai::hwtest::camera
