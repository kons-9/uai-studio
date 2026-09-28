#ifndef UAI_AI_MODEL_FACADE_HPP
#define UAI_AI_MODEL_FACADE_HPP

#include <cstddef>

#include "common/error.hpp"
#include "models/model.hpp"

namespace uai::ai {

/*
 * ModelFacade is the scheduler-owned model boundary. It stores registered
 * model contracts and forwards decoder setup/completion to the selected
 * concrete model. It deliberately does not own NPU execution or scheduling
 * policy.
 */
class ModelFacade final {
public:
    common::Error RegisterModel(const models::ModelBinding &binding);

    std::size_t BindingCount() const { return binding_count_; }
    const models::ModelBinding *BindingAt(std::size_t index) const;
    const models::ModelBinding *Find(models::ModelKind kind) const;

    common::Error ConfigureDecoder(models::ModelKind kind,
                                   const models::ModelOutputSpec &spec) const;
    common::Error Decode(models::ModelKind kind,
                         const models::InferenceCompletionContext &context,
                         models::ModelResult *result) const;

private:
    static constexpr std::size_t kMaxRegisteredModels = 4U;

    models::ModelBinding bindings_[kMaxRegisteredModels]{};
    std::size_t binding_count_ = 0U;
};

} // namespace uai::ai

#endif
