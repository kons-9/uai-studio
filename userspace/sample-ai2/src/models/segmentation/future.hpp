#ifndef UAI_AI_MODELS_SEGMENTATION_FUTURE_HPP
#define UAI_AI_MODELS_SEGMENTATION_FUTURE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "middleware/ai_runtime/pipeline_types.hpp"
#include "driver/driver_ownership.hpp"
#include "application/pipeline/frame_types.hpp"
#include "middleware/memory/buffer_types.hpp"
#include "models/inference_result_types.hpp"
#include "stai.h"

namespace uai::ai::cache {
class CacheManagement;
}

namespace uai::ai::npu {
class NpuDriver;
class NpuNetwork;
}

namespace uai::ai::models::segmentation {

using PublishCallback = void (*)(void *, const inference::BoxSet &);

struct FutureContext {
    npu::NpuDriver *npu = nullptr;
    const driver::ResourceManagement::Writer *npu_writer = nullptr;
    npu::NpuNetwork *model = nullptr;
    std::uint32_t model_kind_id = 0U;
    cache::CacheManagement *cache = nullptr;
    const stai_network_info *info = nullptr;
    PublishCallback publish = nullptr;
    void *publish_context = nullptr;
};

class Future final : public ai_runtime::AiFuture {
public:
    static constexpr std::uint32_t kInputWidth = 320U;
    static constexpr std::uint32_t kInputHeight = 320U;

    enum class Phase : std::uint32_t {
        kPreprocess,
        kNpu,
        kPostprocess,
    };

    static constexpr std::size_t InputBytes()
    {
        return static_cast<std::size_t>(kInputWidth) * kInputHeight * 3U;
    }

    static common::Error ConfigureDecoder(const stai_network_info &info);

    void Reset(const FutureContext &context,
               const pipeline::InferenceFrame &frame);
    bool TryClaim();
    void ReleaseClaim();
    const pipeline::InferenceFrame &frame() const { return frame_; }

    ai_runtime::AiModelId model_id() const override;
    std::uint32_t step_id() const override;
    bool is_ready() const override { return true; }
    ai_runtime::AiRuntimeResult Evaluate() override;

private:
    common::Error Preprocess();
    common::Error Infer();
    common::Error Postprocess();

    FutureContext context_{};
    pipeline::InferenceFrame frame_{};
    Phase phase_ = Phase::kPreprocess;
    std::atomic<bool> occupied_{false};
    std::uint8_t mask_buffer_index_ = 0U;
    bool preprocess_stage_logged_ = false;
    bool infer_stage_logged_ = false;
    bool postprocess_stage_logged_ = false;
};

} // namespace uai::ai::models::segmentation

#endif
