#ifndef UAI_AI_MEMORY_MANAGER_MEMORY_SIZES_HPP
#define UAI_AI_MEMORY_MANAGER_MEMORY_SIZES_HPP

#include <cstddef>

#include "middleware/pipeline/image_format.hpp"
#include "middleware/memory/generated/memory_config.hpp"

namespace uai::ai::memory_manager {

inline constexpr std::size_t kCaptureBufferBytes =
    pipeline::kCaptureFormat.bytes();
inline constexpr std::size_t kInferenceFrameBytes =
    pipeline::kInferenceFormat.bytes();
inline constexpr std::size_t kInferenceScratchBytes =
    pipeline::kInferenceContentFormat.bytes();
/* The source region is a padded square inference image. */
inline constexpr std::size_t kInferenceSourceBytes = kInferenceFrameBytes;
inline constexpr std::size_t kInferenceOutputsOffset =
    kMemoryConfig.AlignUp(kInferenceFrameBytes);
inline constexpr std::size_t kInferenceBufferBytes = kMemoryConfig.AlignUp(
    kInferenceOutputsOffset + kMemoryConfig.inference_output_storage_bytes());

} // namespace uai::ai::memory_manager

#endif
