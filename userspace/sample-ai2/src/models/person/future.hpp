#ifndef UAI_AI_MODELS_PERSON_FUTURE_HPP
#define UAI_AI_MODELS_PERSON_FUTURE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "middleware/ai_runtime/pipeline_types.hpp"
#include "application/pipeline/frame_types.hpp"
#include "middleware/memory/buffer_types.hpp"
#include "models/inference_result_types.hpp"
#include "stai.h"

namespace uai::ai::cache {
class CacheDriver;
}

namespace uai::ai::npu {
class NpuDriver;
class NpuNetwork;
}

namespace uai::ai::models::person {

using PublishCallback = void (*)(void *, const inference::BoxSet &);

/* Runtime resources supplied by the application when a frame is submitted.
 * The Future owns the inference state; the application owns these services. */
struct FutureContext {
    npu::NpuDriver *npu = nullptr;
    npu::NpuNetwork *model = nullptr;
    std::uint32_t model_kind_id = 0U;
    cache::CacheDriver *cache = nullptr;
    const stai_network_info *info = nullptr;
    PublishCallback publish = nullptr;
    void *publish_context = nullptr;
};

/* A complete person inference. PipelineRuntime only advances this object
 * through its three execution lanes; it does not know model-specific stages. */
class Future final : public ai_runtime::AiFuture {
public:
    static constexpr std::uint32_t kInputWidth = 480U;
    static constexpr std::uint32_t kInputHeight = 480U;

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
    bool preprocess_stage_logged_ = false;
    bool infer_stage_logged_ = false;
    bool postprocess_stage_logged_ = false;
};

} // namespace uai::ai::models::person

#endif
