#ifndef UAI_AI_MIDDLEWARE_PIPELINE_IMAGE_FORMAT_HPP
#define UAI_AI_MIDDLEWARE_PIPELINE_IMAGE_FORMAT_HPP

#include <cstddef>
#include <cstdint>

namespace uai::ai::pipeline {

/* Describes a pixel stream or tensor-sized image. The format is a pipeline
 * contract; memory sizes are derived from it by the memory manager. */
struct ImageFormat {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t bytes_per_pixel = 0U;

    constexpr std::size_t bytes() const
    {
        return static_cast<std::size_t>(width) * height * bytes_per_pixel;
    }
};

inline constexpr ImageFormat kCaptureFormat{800U, 480U, 2U};
inline constexpr ImageFormat kInferenceFormat{480U, 480U, 3U};
/* Pipe2 carries a 480x288 aspect-preserving image inside the square
 * inference buffer. The memory manager adds the vertical padding. */
inline constexpr ImageFormat kInferenceContentFormat{480U, 288U, 3U};

} // namespace uai::ai::pipeline

#endif
