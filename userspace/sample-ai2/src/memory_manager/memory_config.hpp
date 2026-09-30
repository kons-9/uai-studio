#ifndef UAI_AI_MEMORY_MANAGER_MEMORY_CONFIG_HPP
#define UAI_AI_MEMORY_MANAGER_MEMORY_CONFIG_HPP

#include <array>
#include <cstddef>

namespace uai::ai::memory_manager {

/* Memory policy and output-storage capacities. Image dimensions are defined
 * by the pipeline formats and are intentionally not part of this type. */
struct MemoryConfig {
    std::size_t buffer_alignment = 32U;

    /* These capacities are shared by the registered model set. The generated
     * model metadata can replace this table without changing pipeline types. */
    std::array<std::size_t, 4U> model_output_bytes = {
        8192U, 60U * 60U * 18U, 30U * 30U * 18U, 384U * 16U};

    constexpr std::size_t AlignUp(std::size_t value) const
    {
        return (value + buffer_alignment - 1U) / buffer_alignment *
               buffer_alignment;
    }

    constexpr std::size_t inference_output_storage_bytes() const
    {
        std::size_t total = 0U;
        for (const std::size_t output_bytes : model_output_bytes) {
            total += AlignUp(output_bytes);
        }
        return total;
    }
};

inline constexpr MemoryConfig kMemoryConfig{};
inline constexpr std::size_t kCaptureBufferCount = 2U;
inline constexpr std::size_t kDisplayBufferCount = 2U;
inline constexpr std::size_t kInferenceBufferCount = 3U;

} // namespace uai::ai::memory_manager

#endif
