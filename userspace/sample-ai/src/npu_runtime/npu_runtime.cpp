#include "npu_runtime/npu_runtime.hpp"

#include "common/log.hpp"

namespace uai::ai::npu_runtime {

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

    inference_started_ = false;
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "ai.npu_runtime.initialize"};
}

common::Error NpuRuntime::Run(memory_allocator::InferenceFrame &frame,
                              memory_allocator::BoxSet *result)
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "ai.npu_runtime.run"};
    }

    if (inference_started_) {
        common::Error status = scheduler_.SelectNext();
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
    }

    const common::Error status = dispatcher_.TryInfer(frame, result);
    last_npu_status_ = dispatcher_.LastNpuStatus();
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

    const common::Error dispatcher_status = dispatcher_.Shutdown();
    last_npu_status_ = npu_.Shutdown();
    const common::Error scheduler_status = scheduler_.Shutdown();
    initialized_ = false;
    inference_started_ = false;
    return !dispatcher_status.Ok() ? dispatcher_status : scheduler_status;
}

const npu::Status &NpuRuntime::LastNpuStatus() const
{
    return dispatcher_.Initialized() ? dispatcher_.LastNpuStatus()
                                     : last_npu_status_;
}

} // namespace uai::ai::npu_runtime
