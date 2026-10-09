#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uai::ai::middleware::ai_model_monitor {

class TraceStepCorrelator final {
public:
    static constexpr std::size_t kCapacity = 16U;

    void Reset() { active_steps_.fill({}); }

    bool Observe(
        std::uint32_t inference_id,
        std::uint32_t step_id,
        std::uint32_t timestamp_ms,
        bool begin,
        std::uint32_t *elapsed_ms
    )
    {
        if (elapsed_ms == nullptr)
            return false;
        *elapsed_ms = 0U;

        ActiveStep *match = nullptr;
        ActiveStep *free_step = nullptr;
        ActiveStep *oldest = nullptr;
        for (ActiveStep &step : active_steps_) {
            if (step.valid && step.inference_id == inference_id && step.step_id == step_id) {
                match = &step;
                break;
            }
            if (!step.valid && free_step == nullptr)
                free_step = &step;
            if (step.valid && (oldest == nullptr || timestamp_ms - step.begin_ms > timestamp_ms - oldest->begin_ms)) {
                oldest = &step;
            }
        }

        if (begin) {
            ActiveStep *step = match != nullptr ? match : free_step != nullptr ? free_step : oldest;
            if (step != nullptr) {
                *step = {inference_id, step_id, timestamp_ms, true};
            }
            return false;
        }
        if (match == nullptr)
            return false;
        *elapsed_ms = timestamp_ms - match->begin_ms;
        match->valid = false;
        return true;
    }

private:
    struct ActiveStep {
        std::uint32_t inference_id = 0U;
        std::uint32_t step_id = 0U;
        std::uint32_t begin_ms = 0U;
        bool valid = false;
    };

    std::array<ActiveStep, kCapacity> active_steps_{};
};

} // namespace uai::ai::middleware::ai_model_monitor
