#ifndef UAI_AI_MODEL_DESCRIPTOR_HPP
#define UAI_AI_MODEL_DESCRIPTOR_HPP

#include <cstddef>
#include <cstdint>

namespace uai::ai::model_manager {

enum class ModelKind : std::uint8_t {
    kPerson,
    kSegmentation,
    kFace,
};

struct ModelDescriptor {
    ModelKind kind;
    const char *name;
    std::uint32_t input_width;
    std::uint32_t input_height;
    std::uint16_t output_count;
    std::size_t output_bytes[4];
    bool input_from_pipe2;
};

inline constexpr ModelDescriptor kPersonModel{
    ModelKind::kPerson, "person", 480U, 480U, 3U,
    {15U * 15U * 18U, 60U * 60U * 18U, 30U * 30U * 18U, 0U}, true};
inline constexpr ModelDescriptor kSegmentationModel{
    ModelKind::kSegmentation, "segmentation", 320U, 320U, 1U,
    {320U * 320U * 2U, 0U, 0U, 0U}, true};
inline constexpr ModelDescriptor kFaceModel{
    ModelKind::kFace, "face", 128U, 128U, 4U,
    {512U * 16U, 512U, 384U, 384U * 16U}, true};

inline constexpr const ModelDescriptor &Describe(ModelKind kind)
{
    switch (kind) {
    case ModelKind::kSegmentation:
        return kSegmentationModel;
    case ModelKind::kFace:
        return kFaceModel;
    case ModelKind::kPerson:
    default:
        return kPersonModel;
    }
}

} // namespace uai::ai::model_manager

#endif
