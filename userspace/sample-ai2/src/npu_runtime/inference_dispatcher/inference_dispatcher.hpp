#ifndef UAI_AI_NPU_RUNTIME_INFERENCE_DISPATCHER_HPP
#define UAI_AI_NPU_RUNTIME_INFERENCE_DISPATCHER_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "pipeline/model_pipeline.hpp"
#include "npu_runtime/inference_timing.hpp"
#include "npu_runtime/scheduler/scheduler.hpp"

namespace uai::ai::npu_runtime {

/* NPU 完了時点の引き継ぎ票。モデル、出力の参照先、デコード条件を固定して
 * 後処理タスクへ送る。バッファ本体の複製ではないため、後処理が終わるまで
 * 対応するフレームの所有権を解放してはならない。 */
struct InferenceCompletion {
    memory_allocator::InferenceFrame frame{};
    pipeline::Handoff handoff{};
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
 * Owns the NPU task's per-inference protocol (cache handoff, dynamic outputs,
 * submit, poll/IRQ/continue). A separate CPU input task has already prepared
 * the frame before BeginInference; postprocessing belongs to another task.
 * The synchronous TryInfer/CompleteInference convenience path is not used by
 * the application's task pipeline.
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
                                 bool select_model = false);
    common::Error WaitForInference(InferenceCompletion *completion);
    common::Error CompleteInference(const InferenceCompletion &completion,
                                    memory_allocator::BoxSet *result);
    common::Error TryInfer(memory_allocator::InferenceFrame &frame,
                           memory_allocator::BoxSet *result,
                           bool select_model = false);
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
    /* 1 回の推論の可変状態。Plan（不変の実行順）に対し、現在位置、
     * 実行中 stage、完了フラグ、計測値を保持する。
     * irq_wait と epoch_continue の反復中は stage_index を固定したまま
     * stage を切り替えるため、両者は常に一致するとは限らない。 */
    struct PipelineState {
        InferenceDispatcher *dispatcher = nullptr;
        memory_allocator::InferenceFrame *frame = nullptr;
        bool select_model = false;
        const pipeline::Plan *pipeline = nullptr;
        std::size_t stage_index = 0U;
        pipeline::Stage stage = pipeline::Stage::kInputCache;
        bool npu_completed = false;
        std::uint32_t input_preparation_start_ms = 0U;
        std::uint32_t input_preparation_end_ms = 0U;
        std::uint32_t input_preparation_elapsed_ms = 0U;
    };

    static common::Error ExecuteModelSelection(void *context);
    static common::Error ExecuteInputHandoff(void *context);
    static common::Error ExecuteNpuSubmit(void *context);
    static common::Error ExecuteNpuIrqWait(void *context);
    static common::Error ExecuteNpuEpochContinue(void *context);
    static common::Error ExecuteInferenceFinalize(void *context);
    common::Error ExecuteStage(PipelineState &state,
                               pipeline::Stage *executed_stage);
    common::Error BuildCompletion(const PipelineState &state,
                                  InferenceCompletion *completion) const;
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

#endif
