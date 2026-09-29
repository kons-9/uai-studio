#include "npu_runtime/npu_runtime.hpp"

#include "common/log.hpp"

namespace uai::ai::npu_runtime {

namespace {

void MergeTiming(InferenceTiming &destination,
                 const InferenceTiming &source)
{
    destination.sequence = source.sequence;
    for (std::size_t i = 0U; i < kInferencePhaseCount; ++i) {
        if (source.phases[i].valid) {
            destination.phases[i] = source.phases[i];
        }
    }
}

class MonitoredOperation final {
public:
    explicit MonitoredOperation(ThreadMonitor &monitor) : monitor_(monitor)
    {
        monitor_.BeginOperation();
    }

    ~MonitoredOperation()
    {
        if (active_) {
            (void)monitor_.EndOperation();
        }
    }

    bool Finish()
    {
        if (!active_) {
            return monitor_.Faulted();
        }
        active_ = false;
        return monitor_.EndOperation();
    }

private:
    ThreadMonitor &monitor_;
    bool active_ = true;
};

common::Error ThreadMonitorTimeout()
{
    return {common::ErrorCode::kTimeout, 0U,
            "ai.npu_runtime.thread_monitor"};
}

void ObserveNpuEpoch(void *context, std::uint32_t model_kind_id,
                     std::uint32_t end_ms, std::uint32_t end_cycles,
                     std::uint32_t elapsed_cycles,
                     std::uint32_t epoch_index, std::uint32_t epoch_flags,
                     std::uintptr_t epoch_address,
                     std::uint32_t callback_type)
{
    auto *monitor = static_cast<ThreadMonitor *>(context);
    if (monitor != nullptr) {
        monitor->ObserveNpuEpoch(
            end_ms, end_cycles, elapsed_cycles, model_kind_id, epoch_index,
            epoch_flags, static_cast<std::uint32_t>(epoch_address),
            callback_type);
    }
}

void ObservePipelineStage(void *context, std::uint32_t end_ms,
                          std::uint32_t end_cycles,
                          std::uint32_t elapsed_cycles,
                          std::uint32_t model_kind_id,
                          std::uint32_t stage_id)
{
    auto *monitor = static_cast<ThreadMonitor *>(context);
    if (monitor != nullptr) {
        monitor->ObservePipelineStage(end_ms, end_cycles, elapsed_cycles,
                                      stage_id, model_kind_id);
    }
}

} // namespace

common::Error NpuRuntime::RegisterModel(const models::ModelBinding &binding)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai.npu_runtime.register"};
    }
    return scheduler_.RegisterModel(binding);
}

common::Error NpuRuntime::Initialize(cache::CacheDriver &cache)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "ai.npu_runtime.initialize"};
    }

    common::Error status = scheduler_.Initialize();
    if (!status.Ok()) {
        return status;
    }

    const models::ModelBinding *active = scheduler_.CurrentBinding();
    if (active == nullptr || active->runtime == nullptr) {
        (void)scheduler_.Shutdown();
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.npu_runtime.active_model"};
    }
    npu_.SetEpochTraceObserver(&ObserveNpuEpoch, &thread_monitor_);
    npu_.SetEpochTraceModelKindId(static_cast<std::uint32_t>(active->kind));
    last_npu_status_ = npu_.Initialize(*active->runtime);
    if (!last_npu_status_.Ok()) {
        (void)scheduler_.Shutdown();
        return last_npu_status_.error;
    }
    for (std::size_t i = 1U; i < scheduler_.BindingCount(); ++i) {
        const models::ModelBinding *binding = scheduler_.BindingAt(i);
        if (binding == nullptr || binding->runtime == nullptr) {
            (void)npu_.Shutdown();
            (void)scheduler_.Shutdown();
            return {common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(i),
                    "ai.npu_runtime.null_model"};
        }
        last_npu_status_ = npu_.Preload(*binding->runtime);
        if (!last_npu_status_.Ok()) {
            (void)npu_.Shutdown();
            (void)scheduler_.Shutdown();
            return last_npu_status_.error;
        }
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: model preloaded=%s\n"),
                     reinterpret_cast<const UB *>(
                         binding->model->GetDescriptor().name));
    }

    status = dispatcher_.Initialize(scheduler_, npu_, cache);
    if (!status.Ok()) {
        (void)npu_.Shutdown();
        (void)scheduler_.Shutdown();
        return status;
    }

    dispatcher_.SetPipelineStageObserver(&ObservePipelineStage,
                                         &thread_monitor_);

    status = thread_monitor_.Start();
    if (!status.Ok()) {
        (void)dispatcher_.Shutdown();
        (void)npu_.Shutdown();
        (void)scheduler_.Shutdown();
        return status;
    }

    inference_started_ = false;
    last_inference_timing_.Reset();
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "ai.npu_runtime.initialize"};
}

common::Error NpuRuntime::Run(memory_allocator::InferenceFrame &frame,
                              memory_allocator::BoxSet *result,
                              PrefetchProvider prefetch_provider,
                              void *prefetch_context)
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.npu_runtime.run"};
    }
    if (thread_monitor_.Faulted()) {
        return ThreadMonitorTimeout();
    }

    MonitoredOperation monitor_operation(thread_monitor_);
    last_inference_timing_.Reset();

    const bool select_model = inference_started_ || frame.input_prepared;
    /* The pipeline owns model selection now. Keep the monitor alive while the
     * first CPU stage configures the selected runtime. */
    thread_monitor_.Progress();

    const common::Error status = dispatcher_.TryInfer(
        frame, result, prefetch_provider, prefetch_context, select_model);
    last_npu_status_ = dispatcher_.LastNpuStatus();
    MergeTiming(last_inference_timing_, dispatcher_.LastTiming());
    const models::ModelDescriptor *descriptor = scheduler_.GetDescriptor();
    const std::uint32_t model_kind_id =
        descriptor == nullptr
            ? kUnknownModelKindId
            : static_cast<std::uint32_t>(descriptor->kind);
    for (std::size_t i = 0U; i < kInferencePhaseCount; ++i) {
        const auto &phase = last_inference_timing_.phases[i];
        if (!phase.valid) {
            continue;
        }
        thread_monitor_.ObserveInferencePhase(
            phase.end_ms, phase.elapsed_ms,
            static_cast<InferencePhase>(i + 1U), model_kind_id);
    }
    /* Keep UART traffic low enough not to perturb the measured execution.
     * One line every eight inferences is enough to compare the async STAI
     * wait components with the phase trace copied from internal RAM. */
    if (status.Ok() && (last_inference_timing_.sequence % 8U) == 0U) {
        const auto &execution = last_npu_status_.execution;
        const auto &input = last_inference_timing_.At(
            InferencePhase::kInputPreparation);
        const auto &input_wait = last_inference_timing_.At(
            InferencePhase::kInputPreparationWait);
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: timing model=%s seq=%u input=%u wait=%u "
                         "npu=%u submit=%u polls=%u irq=%u waits=%u/%u/%u "
                         "continue=%u/%u max=%u slow=%u "
                         "prefetch=%u/%u max=%u\n"),
                     descriptor == nullptr
                         ? reinterpret_cast<const UB *>("unknown")
                         : reinterpret_cast<const UB *>(descriptor->name),
                     static_cast<unsigned int>(last_inference_timing_.sequence),
                     static_cast<unsigned int>(input.elapsed_ms),
                     static_cast<unsigned int>(input_wait.elapsed_ms),
                     static_cast<unsigned int>(execution.elapsed_ms),
                     static_cast<unsigned int>(execution.submit_elapsed_ms),
                     static_cast<unsigned int>(execution.status_poll_count),
                     static_cast<unsigned int>(execution.irq_count_end -
                                               execution.irq_count_start),
                     static_cast<unsigned int>(execution.irq_wait_count),
                     static_cast<unsigned int>(execution.irq_wait_elapsed_ms),
                     static_cast<unsigned int>(execution.irq_wait_max_elapsed_ms),
                     static_cast<unsigned int>(execution.continue_count),
                     static_cast<unsigned int>(execution.continue_elapsed_ms),
                     static_cast<unsigned int>(execution.continue_max_elapsed_ms),
                     static_cast<unsigned int>(execution.continue_slow_count),
                     static_cast<unsigned int>(execution.progress_count),
                     static_cast<unsigned int>(execution.progress_elapsed_ms),
                     static_cast<unsigned int>(execution.progress_max_elapsed_ms));
    }
    if (monitor_operation.Finish()) {
        return ThreadMonitorTimeout();
    }
    if (status.Ok()) {
        inference_started_ = true;
    }
    return status;
}

common::Error NpuRuntime::Shutdown()
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.npu_runtime.shutdown"};
    }

    const common::Error monitor_status = thread_monitor_.Stop();
    npu_.SetEpochTraceObserver(nullptr, nullptr);
    dispatcher_.SetPipelineStageObserver(nullptr, nullptr);
    const common::Error dispatcher_status = dispatcher_.Shutdown();
    last_npu_status_ = npu_.Shutdown();
    const common::Error scheduler_status = scheduler_.Shutdown();
    initialized_ = false;
    inference_started_ = false;
    last_inference_timing_.Reset();
    if (!monitor_status.Ok()) {
        return monitor_status;
    }
    return !dispatcher_status.Ok() ? dispatcher_status : scheduler_status;
}

const npu::Status &NpuRuntime::LastNpuStatus() const
{
    return dispatcher_.Initialized() ? dispatcher_.LastNpuStatus()
                                     : last_npu_status_;
}

} // namespace uai::ai::npu_runtime
