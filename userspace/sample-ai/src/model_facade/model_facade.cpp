#include "model_facade/model_facade.hpp"

namespace uai::ai {

common::Error ModelFacade::RegisterModel(const models::ModelBinding &binding)
{
    if (binding.model == nullptr || binding.runtime == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "ai.model_facade.binding"};
    }
    if (binding_count_ >= kMaxRegisteredModels) {
        return {common::ErrorCode::kNoBuffer,
                static_cast<std::uint32_t>(binding_count_),
                "ai.model_facade.capacity"};
    }
    if (Find(binding.kind) != nullptr) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(binding.kind),
                "ai.model_facade.duplicate"};
    }

    const models::ModelDescriptor &descriptor = binding.model->GetDescriptor();
    if (descriptor.kind != binding.kind) {
        return {common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(binding.kind),
                "ai.model_facade.kind_mismatch"};
    }
    bindings_[binding_count_++] = binding;
    return {common::ErrorCode::kOk, 0U, "ai.model_facade.register"};
}

const models::ModelBinding *ModelFacade::BindingAt(std::size_t index) const
{
    return index < binding_count_ ? &bindings_[index] : nullptr;
}

const models::ModelBinding *ModelFacade::Find(models::ModelKind kind) const
{
    for (std::size_t i = 0U; i < binding_count_; ++i) {
        if (bindings_[i].kind == kind) {
            return &bindings_[i];
        }
    }
    return nullptr;
}

common::Error ModelFacade::ConfigureDecoder(
    models::ModelKind kind, const models::ModelOutputSpec &spec) const
{
    const models::ModelBinding *binding = Find(kind);
    if (binding == nullptr || binding->model == nullptr) {
        return {common::ErrorCode::kModel, static_cast<std::uint32_t>(kind),
                "ai.model_facade.decoder_model"};
    }
    const models::ModelCallbacks callbacks = binding->model->GetCallbacks();
    if (callbacks.configure == nullptr || callbacks.user_data == nullptr) {
        return {common::ErrorCode::kModel, static_cast<std::uint32_t>(kind),
                "ai.model_facade.decoder_missing"};
    }
    return callbacks.configure(spec, callbacks.user_data);
}

common::Error ModelFacade::Decode(
    models::ModelKind kind, const models::InferenceCompletionContext &context,
    models::ModelResult *result) const
{
    const models::ModelBinding *binding = Find(kind);
    if (binding == nullptr || binding->model == nullptr) {
        return {common::ErrorCode::kModel, static_cast<std::uint32_t>(kind),
                "ai.model_facade.decode_model"};
    }
    const models::ModelCallbacks callbacks = binding->model->GetCallbacks();
    if (callbacks.on_inference_complete == nullptr ||
        callbacks.user_data == nullptr) {
        return {common::ErrorCode::kModel, static_cast<std::uint32_t>(kind),
                "ai.model_facade.decoder_missing"};
    }
    return callbacks.on_inference_complete(context, result,
                                            callbacks.user_data);
}

} // namespace uai::ai
