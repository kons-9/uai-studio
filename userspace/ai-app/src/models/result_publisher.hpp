#pragma once

#include <cstdint>

#include "middleware/ai_runtime/inference_result_types.hpp"

namespace uai::ai::models {

using PublishCallback = void (*)(
    void *,
    const inference::BoxSet &,
    std::uint32_t
);

inline void PublishCapturedResult(
    PublishCallback publish,
    void *context,
    inference::BoxSet &result,
    std::uint32_t capture_sequence,
    std::uint32_t generation
)
{
    result.capture_sequence = capture_sequence;
    publish(context, result, generation);
}

}