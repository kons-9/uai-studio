#ifndef UAI_TEST_MEMORY_CONFIG_HPP
#define UAI_TEST_MEMORY_CONFIG_HPP

#include <array>
#include <cstddef>

namespace uai::ai::memory_manager {

struct MemoryConfig {
    std::size_t buffer_alignment = 32U;
    std::array<std::size_t, 2U> model_output_bytes{128U, 64U};

    constexpr std::size_t AlignUp(std::size_t value) const
    {
        return (value + buffer_alignment - 1U) /
               buffer_alignment * buffer_alignment;
    }

    constexpr std::size_t inference_output_storage_bytes() const
    {
        std::size_t total = 0U;
        for (const std::size_t bytes : model_output_bytes) total += AlignUp(bytes);
        return total;
    }
};

inline constexpr MemoryConfig kMemoryConfig{};
inline constexpr std::size_t kCaptureBufferCount = 2U;
inline constexpr std::size_t kDisplayBufferCount = 2U;
inline constexpr std::size_t kInferenceBufferCount = 3U;
inline constexpr std::size_t kInferenceSourceBufferCount = 3U;

} // namespace uai::ai::memory_manager

#endif