#ifndef UAI_AI_MODELS_SEGMENTATION_SEGMENTATION_DECODER_HPP
#define UAI_AI_MODELS_SEGMENTATION_SEGMENTATION_DECODER_HPP

#include <cstdint>

#include "models/model.hpp"

namespace uai::ai::models::segmentation {

/* Converts the raw two-class logits into the displayable binary mask. */
class Decoder final {
public:
    common::Error Initialize(const ModelOutputSpec &spec);
    common::Error Decode(const InferenceCompletionContext &context,
                         ModelResult *result);

private:
    std::uint8_t mask_buffer_index_ = 0U;
    bool initialized_ = false;
};

} // namespace uai::ai::models::segmentation

#endif
