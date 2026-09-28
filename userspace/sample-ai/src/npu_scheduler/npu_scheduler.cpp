#include "npu_scheduler/npu_scheduler.hpp"

#include "common/log.hpp"

namespace uai::ai::npu_scheduler {

common::Error NpuScheduler::InvalidState(const char *operation) const
{
    return {common::ErrorCode::kNotInitialized, 0U, operation};
}

common::Error NpuScheduler::RegisterModel(const models::ModelBinding &binding)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "npu_scheduler.register"};
    }
    return model_facade_.RegisterModel(binding);
}

common::Error NpuScheduler::Initialize()
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "npu_scheduler.initialize"};
    }
    if (model_facade_.BindingCount() == 0U) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "npu_scheduler.models"};
    }

    active_ = model_facade_.BindingAt(0U);
    if (active_ == nullptr || active_->runtime == nullptr) {
        active_ = nullptr;
        return {common::ErrorCode::kInvalidArgument, 0U,
                "npu_scheduler.initial_model"};
    }

    last_status_ = npu_.Initialize(*active_->runtime);
    if (!last_status_.Ok()) {
        active_ = nullptr;
        return last_status_.error;
    }

    /* Preload every registered runtime once. SelectNext only changes the
     * active runtime pointer after this point. */
    for (std::size_t i = 1U; i < model_facade_.BindingCount(); ++i) {
        const models::ModelBinding *binding = model_facade_.BindingAt(i);
        if (binding == nullptr || binding->runtime == nullptr) {
            (void)npu_.Shutdown();
            active_ = nullptr;
            return {common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(i),
                    "npu_scheduler.null_model"};
        }
        last_status_ = npu_.Preload(*binding->runtime);
        if (!last_status_.Ok()) {
            (void)npu_.Shutdown();
            active_ = nullptr;
            return last_status_.error;
        }
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: model preloaded=%s\n"),
                     reinterpret_cast<const UB *>(
                         binding->model->GetDescriptor().name));
    }

    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "npu_scheduler.initialize"};
}

common::Error NpuScheduler::SelectNext()
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_scheduler.select_next");
    }
    if (model_facade_.BindingCount() <= 1U) {
        return {common::ErrorCode::kOk, 0U,
                "npu_scheduler.select_next"};
    }

    std::size_t active_index = 0U;
    for (; active_index < model_facade_.BindingCount(); ++active_index) {
        if (model_facade_.BindingAt(active_index) == active_) {
            break;
        }
    }
    if (active_index >= model_facade_.BindingCount()) {
        return {common::ErrorCode::kInvalidState, 0U,
                "npu_scheduler.active_model"};
    }
    const std::size_t next_index =
        (active_index + 1U) % model_facade_.BindingCount();
    const models::ModelBinding *next = model_facade_.BindingAt(next_index);
    if (next == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(next_index),
                "npu_scheduler.next_model"};
    }
    return SelectModel(next->kind);
}

common::Error NpuScheduler::SelectModel(models::ModelKind kind)
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_scheduler.select");
    }
    if (active_->kind == kind) {
        return {common::ErrorCode::kOk, 0U, "npu_scheduler.select"};
    }
    const models::ModelBinding *binding = model_facade_.Find(kind);
    if (binding == nullptr || binding->runtime == nullptr) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(kind),
                "npu_scheduler.model_not_registered"};
    }
    last_status_ = npu_.SelectModel(*binding->runtime);
    if (!last_status_.Ok()) {
        return last_status_.error;
    }
    active_ = binding;
    return {common::ErrorCode::kOk, 0U, "npu_scheduler.select"};
}

models::ModelKind NpuScheduler::CurrentModel() const
{
    return active_ != nullptr ? active_->kind : models::ModelKind::kPerson;
}

const models::ModelDescriptor *NpuScheduler::GetDescriptor() const
{
    if (!initialized_ || active_ == nullptr || active_->model == nullptr) {
        return nullptr;
    }
    return &active_->model->GetDescriptor();
}

common::Error NpuScheduler::GetInfo(stai_network_info *info) const
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.get_info");
    }
    return npu_.GetInfo(info).error;
}

common::Error NpuScheduler::GetOutputs(stai_ptr *outputs, stai_size *count) const
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.get_outputs");
    }
    return npu_.GetOutputs(outputs, count).error;
}

common::Error NpuScheduler::SetInput(stai_ptr input, stai_size size) const
{
    if (!initialized_ || active_ == nullptr || active_->runtime == nullptr) {
        return InvalidState("npu_scheduler.set_input");
    }
    const stai_return_code code = active_->runtime->SetInput(input, size);
    return code >= STAI_ERROR_GENERIC
               ? common::Error{common::ErrorCode::kModel,
                               static_cast<std::uint32_t>(code),
                               "npu_scheduler.set_input"}
               : common::Error{common::ErrorCode::kOk, 0U,
                               "npu_scheduler.set_input"};
}

common::Error NpuScheduler::SetOutputs(const stai_ptr *outputs,
                                       stai_size count) const
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.set_outputs");
    }
    return npu_.SetOutputs(outputs, count).error;
}

common::Error NpuScheduler::ConfigureActiveModel(
    const models::ModelOutputSpec &spec)
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_scheduler.configure_model");
    }
    return model_facade_.ConfigureDecoder(active_->kind, spec);
}

common::Error NpuScheduler::DecodeActive(
    const models::InferenceCompletionContext &context,
    models::ModelResult *result) const
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_scheduler.decode_model");
    }
    return model_facade_.Decode(active_->kind, context, result);
}

common::Error NpuScheduler::Run()
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.run");
    }
    last_status_ = npu_.Run();
    return last_status_.error;
}

common::Error NpuScheduler::NewInference()
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.new_inference");
    }
    last_status_ = npu_.NewInference();
    return last_status_.error;
}

common::Error NpuScheduler::Shutdown()
{
    if (!initialized_) {
        return InvalidState("npu_scheduler.shutdown");
    }
    last_status_ = npu_.Shutdown();
    initialized_ = false;
    active_ = nullptr;
    model_facade_ = {};
    return last_status_.error;
}

} // namespace uai::ai::npu_scheduler
