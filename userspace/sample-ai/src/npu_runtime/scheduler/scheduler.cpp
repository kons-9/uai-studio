#include "npu_runtime/scheduler/scheduler.hpp"

namespace uai::ai::npu_runtime::scheduler {

common::Error Scheduler::InvalidState(const char *operation) const
{
    return {common::ErrorCode::kNotInitialized, 0U, operation};
}

common::Error Scheduler::RegisterModel(const models::ModelBinding &binding)
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "npu_runtime.scheduler.register"};
    }
    if (binding.model == nullptr || binding.runtime == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "npu_runtime.scheduler.binding"};
    }
    if (binding_count_ >= kMaxRegisteredModels) {
        return {common::ErrorCode::kNoBuffer,
                static_cast<std::uint32_t>(binding_count_),
                "npu_runtime.scheduler.capacity"};
    }
    if (Find(binding.kind) != nullptr) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(binding.kind),
                "npu_runtime.scheduler.duplicate"};
    }

    const models::ModelDescriptor &descriptor = binding.model->GetDescriptor();
    if (descriptor.kind != binding.kind) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(binding.kind),
                "npu_runtime.scheduler.kind_mismatch"};
    }
    bindings_[binding_count_++] = binding;
    return {common::ErrorCode::kOk, 0U, "npu_runtime.scheduler.register"};
}

common::Error Scheduler::Initialize()
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized, 0U,
                "npu_runtime.scheduler.initialize"};
    }
    if (binding_count_ == 0U) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "npu_runtime.scheduler.models"};
    }

    active_ = BindingAt(0U);
    if (active_ == nullptr || active_->runtime == nullptr) {
        active_ = nullptr;
        return {common::ErrorCode::kInvalidArgument, 0U,
                "npu_runtime.scheduler.initial_model"};
    }

    initialized_ = true;
    return {common::ErrorCode::kOk, 0U, "npu_runtime.scheduler.initialize"};
}

common::Error Scheduler::SelectNext()
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_runtime.scheduler.select_next");
    }
    if (binding_count_ <= 1U) {
        return {common::ErrorCode::kOk, 0U,
                "npu_runtime.scheduler.select_next"};
    }

    std::size_t active_index = 0U;
    for (; active_index < binding_count_; ++active_index) {
        if (BindingAt(active_index) == active_) {
            break;
        }
    }
    if (active_index >= binding_count_) {
        return {common::ErrorCode::kInvalidState, 0U,
                "npu_runtime.scheduler.active_model"};
    }
    const std::size_t next_index =
        (active_index + 1U) % binding_count_;
    const models::ModelBinding *next = BindingAt(next_index);
    if (next == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(next_index),
                "npu_runtime.scheduler.next_model"};
    }
    return SelectModel(next->kind);
}

common::Error Scheduler::Select(models::ModelKind kind)
{
    return SelectModel(kind);
}

common::Error Scheduler::SelectModel(models::ModelKind kind)
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_runtime.scheduler.select");
    }
    if (active_->kind == kind) {
        return {common::ErrorCode::kOk, 0U, "npu_runtime.scheduler.select"};
    }
    const models::ModelBinding *binding = Find(kind);
    if (binding == nullptr || binding->runtime == nullptr) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(kind),
                "npu_runtime.scheduler.model_not_registered"};
    }
    active_ = binding;
    return {common::ErrorCode::kOk, 0U, "npu_runtime.scheduler.select"};
}

const models::ModelBinding *Scheduler::BindingAt(std::size_t index) const
{
    return index < binding_count_ ? &bindings_[index] : nullptr;
}

const models::ModelBinding *Scheduler::NextBinding() const
{
    if (!initialized_ || active_ == nullptr || binding_count_ == 0U) {
        return nullptr;
    }

    std::size_t active_index = 0U;
    for (; active_index < binding_count_; ++active_index) {
        if (BindingAt(active_index) == active_) {
            break;
        }
    }
    if (active_index >= binding_count_) {
        return nullptr;
    }
    return BindingAt((active_index + 1U) % binding_count_);
}

const models::ModelBinding *Scheduler::Find(models::ModelKind kind) const
{
    for (std::size_t i = 0U; i < binding_count_; ++i) {
        if (bindings_[i].kind == kind) {
            return &bindings_[i];
        }
    }
    return nullptr;
}

const models::ModelDescriptor *Scheduler::GetDescriptor() const
{
    if (!initialized_ || active_ == nullptr || active_->model == nullptr) {
        return nullptr;
    }
    return &active_->model->GetDescriptor();
}

common::Error Scheduler::ConfigureActiveDecoder(
    const models::ModelOutputSpec &spec)
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_runtime.scheduler.configure_decoder");
    }
    if (active_->model == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(active_->kind),
                "npu_runtime.scheduler.decoder_model"};
    }
    const models::ModelCallbacks callbacks = active_->model->GetCallbacks();
    if (callbacks.configure == nullptr || callbacks.user_data == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(active_->kind),
                "npu_runtime.scheduler.decoder_missing"};
    }
    return callbacks.configure(spec, callbacks.user_data);
}

common::Error Scheduler::DecodeActiveOutputs(
    const models::InferenceCompletionContext &context,
    models::ModelResult *result) const
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_runtime.scheduler.decode_outputs");
    }
    if (active_->model == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(active_->kind),
                "npu_runtime.scheduler.decode_model"};
    }
    const models::ModelCallbacks callbacks = active_->model->GetCallbacks();
    if (callbacks.on_inference_complete == nullptr ||
        callbacks.user_data == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(active_->kind),
                "npu_runtime.scheduler.decoder_missing"};
    }
    return callbacks.on_inference_complete(context, result,
                                            callbacks.user_data);
}

common::Error Scheduler::PrepareActiveInput(
    memory_allocator::InferenceFrame &frame, cache::CacheDriver &cache) const
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_runtime.scheduler.prepare_input");
    }
    return PrepareInputFor(*active_, frame, cache);
}

common::Error Scheduler::PrepareInputFor(
    const models::ModelBinding &binding,
    memory_allocator::InferenceFrame &frame, cache::CacheDriver &cache) const
{
    if (!initialized_ || binding.model == nullptr) {
        return InvalidState("npu_runtime.scheduler.prepare_input");
    }
    return binding.model->PrepareInput(frame, cache);
}

common::Error Scheduler::ConvertActiveResult(
    const models::ModelResult &source,
    memory_allocator::BoxSet *destination) const
{
    if (!initialized_ || active_ == nullptr) {
        return InvalidState("npu_runtime.scheduler.convert_result");
    }
    if (active_->model == nullptr) {
        return {common::ErrorCode::kModel,
                static_cast<std::uint32_t>(active_->kind),
                "npu_runtime.scheduler.result_model"};
    }
    return active_->model->ConvertResult(source, destination);
}

common::Error Scheduler::Shutdown()
{
    if (!initialized_) {
        return InvalidState("npu_runtime.scheduler.shutdown");
    }
    initialized_ = false;
    active_ = nullptr;
    for (models::ModelBinding &binding : bindings_) {
        binding = {};
    }
    binding_count_ = 0U;
    return {common::ErrorCode::kOk, 0U, "npu_runtime.scheduler.shutdown"};
}

} // namespace uai::ai::npu_runtime::scheduler
