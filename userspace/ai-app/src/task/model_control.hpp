#pragma once

#include <cstdint>

namespace uai::ai::task {

/* Bit per registered model; the frame task only submits to enabled models. */
enum class ModelBit : std::uint8_t {
    kPerson = 1U << 0U,
    kFace = 1U << 1U,
    kSegmentation = 1U << 2U,
};
inline constexpr std::uint8_t kAllModelsMask = 0x07U;

constexpr std::uint8_t ModelMaskBit(ModelBit bit)
{
    return static_cast<std::uint8_t>(bit);
}

/* Monotonic counters read by the UI; written by the pipeline tasks. */
struct PipelineStats {
    std::uint32_t person_completed = 0U;
    std::uint32_t face_completed = 0U;
    std::uint32_t segmentation_completed = 0U;
    std::uint32_t last_detection_count = 0U;
    bool enabled = false;
};

/* What the on-screen UI needs from the inference pipeline. Kept OS-free so
 * the UI logic can be tested on the host. */
class ModelControl {
public:
    virtual std::uint8_t ModelMask() const = 0;
    virtual void SetModelMask(std::uint8_t mask) = 0;
    virtual PipelineStats Stats() const = 0;

protected:
    ~ModelControl() = default;
};

} // namespace uai::ai::task
