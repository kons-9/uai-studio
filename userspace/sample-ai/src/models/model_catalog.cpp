#include "models/model.hpp"

#include "models/face/model.hpp"
#include "models/person/model.hpp"
#include "models/segmentation/model.hpp"

namespace uai::ai::models {

const ModelDescriptor &DescriptorFor(ModelKind kind)
{
    switch (kind) {
    case ModelKind::kSegmentation:
        return segmentation::Model::Descriptor();
    case ModelKind::kFace:
        return face::Model::Descriptor();
    case ModelKind::kPerson:
    default:
        return person::Model::Descriptor();
    }
}

} // namespace uai::ai::models
