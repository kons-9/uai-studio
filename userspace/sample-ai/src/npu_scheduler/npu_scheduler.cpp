#include "npu_scheduler/npu_scheduler.hpp"

namespace uai::ai::npu_scheduler {

using common::Error;
using common::ErrorCode;
using models::ModelKind;

const ModelBinding *NpuScheduler::Find(ModelKind kind) const
{
    if (bindings_ == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0U; i < binding_count_; ++i) {
        if (bindings_[i].kind == kind && bindings_[i].model != nullptr) {
            return &bindings_[i];
        }
    }
    return nullptr;
}

Error NpuScheduler::InvalidState(const char *operation) const
{
    return {ErrorCode::kNotInitialized, 0U, operation};
}

Error NpuScheduler::Initialize(const ModelBinding *bindings,
                               std::size_t binding_count,
                               ModelKind initial_model)
{
    if (initialized_) {
        return {ErrorCode::kAlreadyInitialized, 0U,
                "npu_scheduler.initialize"};
    }
    if (bindings == nullptr || binding_count == 0U) {
        return {ErrorCode::kInvalidArgument, 0U,
                "npu_scheduler.bindings"};
    }

    bindings_ = bindings;
    binding_count_ = binding_count;
    active_ = Find(initial_model);
    if (active_ == nullptr) {
        return {ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(initial_model),
                "npu_scheduler.initial_model"};
    }

    last_status_ = npu_.Initialize(*active_->model);
    if (!last_status_.Ok()) {
        return last_status_.error;
    }

    /* Load every registered context once. SelectModel only changes the
     * generated context pointer after this point; it does not reload NOR. */
    for (std::size_t i = 0U; i < binding_count_; ++i) {
        if (&bindings_[i] == active_) {
            continue;
        }
        if (bindings_[i].model == nullptr) {
            return {ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(i),
                    "npu_scheduler.null_model"};
        }
        last_status_ = npu_.Preload(*bindings_[i].model);
        if (!last_status_.Ok()) {
            return last_status_.error;
        }
    }

    initialized_ = true;
    return {ErrorCode::kOk, 0U, "npu_scheduler.initialize"};
}

Error NpuScheduler::SelectModel(ModelKind kind)
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_scheduler.select");
    }
    if (active_->kind == kind) {
        return {ErrorCode::kOk, 0U, "npu_scheduler.select"};
    }
    const ModelBinding *binding = Find(kind);
    if (binding == nullptr) {
        return {ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(kind),
                "npu_scheduler.model_not_registered"};
    }
    last_status_ = npu_.SelectModel(*binding->model);
    if (!last_status_.Ok()) {
        return last_status_.error;
    }
    active_ = binding;
    return {ErrorCode::kOk, 0U, "npu_scheduler.select"};
}

ModelKind NpuScheduler::CurrentModel() const
{
    return active_ != nullptr ? active_->kind : ModelKind::kPerson;
}

const models::ModelDescriptor *NpuScheduler::GetDescriptor() const
{
    if (!initialized_ || active_ == nullptr || active_->model == nullptr) {
        return nullptr;
    }
    return &active_->model->GetDescriptor();
}

Error NpuScheduler::GetInfo(stai_network_info *info) const
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.get_info");
    }
    return npu_.GetInfo(info).error;
}

Error NpuScheduler::GetOutputs(stai_ptr *outputs, stai_size *count) const
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.get_outputs");
    }
    return npu_.GetOutputs(outputs, count).error;
}

Error NpuScheduler::SetInput(stai_ptr input, stai_size size) const
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_scheduler.set_input");
    }
    const stai_return_code code = active_->model->SetInput(input, size);
    return code >= STAI_ERROR_GENERIC
               ? Error{ErrorCode::kModel, static_cast<std::uint32_t>(code),
                       "npu_scheduler.set_input"}
               : Error{ErrorCode::kOk, 0U, "npu_scheduler.set_input"};
}

Error NpuScheduler::SetOutputs(const stai_ptr *outputs, stai_size count) const
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.set_outputs");
    }
    return npu_.SetOutputs(outputs, count).error;
}

Error NpuScheduler::Run()
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.run");
    }
    last_status_ = npu_.Run();
    return last_status_.error;
}

Error NpuScheduler::NewInference()
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.new_inference");
    }
    last_status_ = npu_.NewInference();
    return last_status_.error;
}

Error NpuScheduler::Shutdown()
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.shutdown");
    }
    last_status_ = npu_.Shutdown();
    initialized_ = false;
    active_ = nullptr;
    bindings_ = nullptr;
    binding_count_ = 0U;
    return last_status_.error;
}

} // namespace uai::ai::npu_scheduler
