#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::npu_runtime {

enum class InferencePhase : std::uint16_t {
    kModelSelection = 1U,
    kInputPreparation = 2U,
    kNpuExecution = 3U,
    kOutputPreparation = 4U,
    kOutputDecoding = 5U,
    kResultConversion = 6U,
    /* Kept at the end so existing version-3 traces keep their phase IDs. */
    kInputPreparationWait = 7U,
};

inline constexpr std::size_t kInferencePhaseCount = 7U;

constexpr std::size_t InferencePhaseIndex(InferencePhase phase)
{
    return static_cast<std::size_t>(phase) - 1U;
}

struct InferencePhaseTiming {
    std::uint32_t start_ms = 0U;
    std::uint32_t end_ms = 0U;
    std::uint32_t elapsed_ms = 0U;
    bool valid = false;
};

struct InferenceTiming {
    std::uint32_t sequence = 0U;
    InferencePhaseTiming phases[kInferencePhaseCount]{};

    void Reset()
    {
        sequence = 0U;
        for (auto &phase : phases) {
            phase = {};
        }
    }

    InferencePhaseTiming &At(InferencePhase phase)
    {
        return phases[InferencePhaseIndex(phase)];
    }

    const InferencePhaseTiming &At(InferencePhase phase) const
    {
        return phases[InferencePhaseIndex(phase)];
    }
};

} // namespace uai::ai::npu_runtime
