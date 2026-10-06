#pragma once

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "npu_runtime/inference_timing.hpp"
#include "npu_runtime/scheduler/scheduler.hpp"

namespace uai::ai::npu_runtime {

using PrefetchProvider = memory_allocator::InferenceFrame *(*)(void *context);

/* Snapshot of the NPU-owned part of an inference.  The output pointers and
 * model callbacks are copied before the shared NPU dispatcher is advanced to
 * the next model, so CPU completion can run independently afterwards. */
struct InferenceCompletion {
    memory_allocator::InferenceFrame frame{};
    memory_allocator::Buffer output_buffers[models::kMaxModelOutputs]{};
    models::ModelKind model_kind = models::ModelKind::kPerson;
    const models::Model *model = nullptr;
    models::ModelCallbacks callbacks{};
    models::ModelOutputView output_view{};
    models::InferenceGeometry geometry{};
    std::uint32_t model_sequence = 0U;
    std::uint32_t capture_sequence = 0U;
    npu::ExecutionSnapshot execution{};
    bool valid = false;
};

/*
 * Bridges a selected scheduler model to the camera buffers and application
 * result format. It owns the per-inference protocol because input cache
 * maintenance, dynamic outputs, and decoder invocation are too low-level for
 * Scheduler. Model-specific input preparation and result conversion are
 * delegated through the selected Model interface.
 */
class InferenceDispatcher final {
public:
    using PipelineStageObserver = void (*)(
        void *context, std::uint32_t end_ms, std::uint32_t end_cycles,
        std::uint32_t elapsed_cycles, std::uint32_t model_kind_id,
        std::uint32_t stage_id);

    common::Error Initialize(scheduler::Scheduler &scheduler,
                             npu::NpuDriver &npu,
                             cache::CacheDriver &cache);
    common::Error RefreshSelectedModel();
    common::Error BeginInference(memory_allocator::InferenceFrame &frame,
                                 PrefetchProvider prefetch_provider = nullptr,
                                 void *prefetch_context = nullptr,
                                 bool select_model = false);
    common::Error WaitForInference(InferenceCompletion *completion);
    common::Error CompleteInference(const InferenceCompletion &completion,
                                    memory_allocator::BoxSet *result);
    common::Error TryInfer(memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result,
                           PrefetchProvider prefetch_provider = nullptr,
                           void *prefetch_context = nullptr,
                           bool select_model = false);
    common::Error PrepareInputFor(const models::ModelBinding &binding,
                                  memory_allocator::InferenceFrame &frame);
    common::Error Shutdown();

    void SetPipelineStageObserver(PipelineStageObserver observer,
                                  void *context)
    {
        pipeline_stage_observer_ = observer;
        pipeline_stage_context_ = context;
    }

    bool Initialized() const { return initialized_; }
    const npu::Status &LastNpuStatus() const { return last_npu_status_; }
    const InferenceTiming &LastTiming() const { return last_timing_; }

private:
    struct PrefetchState {
        InferenceDispatcher *dispatcher = nullptr;
        PrefetchProvider provider = nullptr;
        void *provider_context = nullptr;
        memory_allocator::InferenceFrame *frame = nullptr;
        common::Error error{};
        bool prepared = false;
    };

    struct PipelineState {
        InferenceDispatcher *dispatcher = nullptr;
        memory_allocator::InferenceFrame *frame = nullptr;
        memory_allocator::BoxSet *result = nullptr;
        PrefetchProvider prefetch_provider = nullptr;
        void *prefetch_context = nullptr;
        bool select_model = false;
        const models::ModelPipeline *pipeline = nullptr;
        std::size_t stage_index = 0U;
        models::ModelStageId stage = models::ModelStageId::kInputCache;
        bool npu_completed = false;
        std::uint32_t input_preparation_start_ms = 0U;
        std::uint32_t input_preparation_end_ms = 0U;
        std::uint32_t input_preparation_elapsed_ms = 0U;
        models::ModelOutputView output_view{};
        models::InferenceGeometry geometry{};
        models::ModelResult decoded_result{};
        PrefetchState prefetch{};
    };

    static void PreparePrefetch(void *context);
    static common::Error ExecuteModelSelection(void *context);
    static common::Error ExecuteModelCpuStage(void *context);
    static common::Error ExecuteInputHandoff(void *context);
    static common::Error ExecuteNpuSubmit(void *context);
    static common::Error ExecuteNpuIrqWait(void *context);
    static common::Error ExecuteNpuEpochContinue(void *context);
    static common::Error ExecuteOutputPreparation(void *context);
    static common::Error ExecuteOutputDecoding(void *context);
    static common::Error ExecuteResultConversion(void *context);
    static common::Error ExecuteInferenceFinalize(void *context);
    common::Error ExecuteStage(PipelineState &state,
                               models::ModelStageId *executed_stage);
    common::Error BuildCompletion(const PipelineState &state,
                                  InferenceCompletion *completion) const;
    static common::Error ExecutePipeline(PipelineState &state);
    common::Error ConfigureCurrentModel();

    scheduler::Scheduler *scheduler_ = nullptr;
    npu::NpuDriver *npu_ = nullptr;
    cache::CacheDriver *cache_ = nullptr;
    stai_network_info info_{};
    stai_ptr outputs_[memory_allocator::kConfig.model_output_bytes.size()]{};
    bool dynamic_outputs_ = false;
    bool decoder_configured_[3]{};
    bool initialized_ = false;
    std::uint32_t model_sequence_ = 0U;
    std::uint32_t last_error_ = 0U;
    npu::Status last_npu_status_{};
    InferenceTiming last_timing_{};
    PipelineState active_pipeline_{};
    bool pipeline_active_ = false;
    PipelineStageObserver pipeline_stage_observer_ = nullptr;
    void *pipeline_stage_context_ = nullptr;
};

} // namespace uai::ai::npu_runtime
