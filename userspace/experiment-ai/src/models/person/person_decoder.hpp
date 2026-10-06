#pragma once

#include <cstddef>

#include "models/model.hpp"

namespace uai::ai::models::person {

/* Converts the raw YOLOX tensors into the model-neutral detection result. */
class Decoder final {
public:
    common::Error Initialize(const ModelOutputSpec &spec);
    common::Error Decode(const InferenceCompletionContext &context,
                         ModelResult *result) const;

private:
    std::size_t output_order_[3]{};
    bool initialized_ = false;
};

} // namespace uai::ai::models::person
