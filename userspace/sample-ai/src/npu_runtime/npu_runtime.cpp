#include "npu_runtime/npu_runtime.hpp"

#include "common/log.hpp"

#include <tk/tkernel.h>

namespace uai::ai::npu_runtime {

namespace {

std::uint32_t NowMs()
{
    SYSTIM time = {};
    return tk_get_otm(&time) == E_OK ? time.lo : 0U;
}

class PhaseTimingScope final {
public:
    explicit PhaseTimingScope(InferencePhaseTiming &timing)
        : timing_(timing)
    {
        timing_.start_ms = NowMs();
        timing_.end_ms = timing_.start_ms;
        timing_.elapsed_ms = 0U;
        timing_.valid = false;
    }

    ~PhaseTimingScope()
    {
        timing_.end_ms = NowMs();
        timing_.elapsed_ms = timing_.end_ms - timing_.start_ms;
        timing_.valid = true;
    }

private:
    InferencePhaseTiming &timing_;
};

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

    if (inference_started_ || frame.input_prepared) {
        PhaseTimingScope phase(
            last_inference_timing_.At(InferencePhase::kModelSelection));
        common::Error status;
        if (frame.input_prepared) {
            const models::ModelBinding *prepared_binding = nullptr;
            for (std::size_t i = 0U; i < scheduler_.BindingCount(); ++i) {
                const models::ModelBinding *binding = scheduler_.BindingAt(i);
                if (binding != nullptr &&
                    static_cast<std::uint8_t>(binding->kind) ==
                        frame.prepared_model_kind_id) {
                    prepared_binding = binding;
                    break;
                }
            }
            if (prepared_binding == nullptr) {
                return {common::ErrorCode::kModel,
                        frame.prepared_model_kind_id,
                        "ai.npu_runtime.prepared_model"};
            }
            status = scheduler_.Select(prepared_binding->kind);
        } else {
            status = scheduler_.SelectNext();
        }
        if (!status.Ok()) {
            return status;
        }
        const models::ModelBinding *active = scheduler_.CurrentBinding();
        if (active == nullptr || active->runtime == nullptr) {
            return {common::ErrorCode::kInvalidArgument, 0U,
                    "ai.npu_runtime.active_model"};
        }
        last_npu_status_ = npu_.SelectModel(*active->runtime);
        if (!last_npu_status_.Ok()) {
            return last_npu_status_.error;
        }
        status = dispatcher_.RefreshSelectedModel();
        if (!status.Ok()) {
            return status;
        }
        const models::ModelDescriptor *descriptor = scheduler_.GetDescriptor();
        if (descriptor != nullptr) {
            UAI_LOG_INFO(reinterpret_cast<const UB *>(
                             "ai: model switched to %s\n"),
                         reinterpret_cast<const UB *>(descriptor->name));
        }
        thread_monitor_.Progress();
    }

    const common::Error status = dispatcher_.TryInfer(
        frame, result, prefetch_provider, prefetch_context);
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
