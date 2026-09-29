#ifndef UAI_AI_MODELS_FACE_FACE_DECODER_HPP
#define UAI_AI_MODELS_FACE_FACE_DECODER_HPP

#include <cstddef>
#include <cstdint>

#include "models/model.hpp"

namespace uai::ai::models::face {

/* Converts the raw BlazeFace tensors into the public model result. The C
 * postprocess library remains private to this decoder; callers use only the
 * model callback interface. */
class Decoder final {
public:
    common::Error Initialize(const ModelOutputSpec &spec);
    common::Error Decode(const InferenceCompletionContext &context,
                         ModelResult *result) const;

private:
    std::size_t output_order_[4]{};
    bool initialized_ = false;
};

} // namespace uai::ai::models::face

#endif
