#pragma once

#include <array>
#include <cstdint>

#include "middleware/ai_runtime/inference_result_types.hpp"

namespace uai::ai::task {

struct ModelResultStamp {
    bool valid = false;
    std::uint32_t completed_ms = 0U;
    std::uint32_t update_sequence = 0U;
};

struct ModelResultSnapshot {
    inference::BoxSet boxes{};
    std::array<ModelResultStamp, 3U> stamps{};
    std::uint32_t generation = 0U;
};

}