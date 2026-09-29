#ifndef UAI_AI_MODELS_PERSON_FUTURE_HPP
#define UAI_AI_MODELS_PERSON_FUTURE_HPP

#include <atomic>
#include <cstdint>

#include "middleware/ai_runtime/pipeline.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "models/person/model.hpp"

namespace uai::ai::cache {
class CacheDriver;
}

namespace uai::ai::npu {
class NpuDriver;
}

namespace uai::ai::models::person {

using PublishCallback = void (*)(void *, const memory_allocator::BoxSet &);

/* Runtime resources supplied by the application when a frame is submitted.
 * The Future owns the inference state; the application owns these services. */
struct FutureContext {
    Model *model = nullptr;
    npu::NpuDriver *npu = nullptr;
    cache::CacheDriver *cache = nullptr;
    const stai_network_info *info = nullptr;
    PublishCallback publish = nullptr;
    void *publish_context = nullptr;
};

/* A complete person inference. PipelineRuntime only advances this object
 * through its three execution lanes; it does not know model-specific stages. */
class Future final : public ai_runtime::AiFuture {
public:
    enum class Phase : std::uint32_t {
        kPreprocess,
        kNpu,
        kPostprocess,
    };

    void Reset(const FutureContext &context,
               const memory_allocator::InferenceFrame &frame);
    bool TryClaim();
    void ReleaseClaim();
    const memory_allocator::InferenceFrame &frame() const { return frame_; }

    ai_runtime::AiModelId model_id() const override;
    std::uint32_t step_id() const override;
    bool is_ready() const override { return true; }
    ai_runtime::AiRuntimeResult Evaluate() override;

private:
    common::Error Preprocess();
    common::Error Infer();
    common::Error Postprocess();

    FutureContext context_{};
    memory_allocator::InferenceFrame frame_{};
    Phase phase_ = Phase::kPreprocess;
    std::atomic<bool> occupied_{false};
    bool preprocess_stage_logged_ = false;
    bool infer_stage_logged_ = false;
    bool postprocess_stage_logged_ = false;
};

} // namespace uai::ai::models::person

#endif
