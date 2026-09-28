#include "models/segmentation/segmentation_decoder.hpp"

#include <cstddef>
#include <cstdint>

#include "static_memory_layout/static_memory_layout.hpp"

namespace uai::ai::models::segmentation {

namespace {

constexpr std::size_t kMaskWidth = 320U;
constexpr std::size_t kMaskHeight = 320U;
constexpr std::size_t kMaskBytes = kMaskWidth * kMaskHeight;

common::Error Invalid(const char *operation)
{
    return {common::ErrorCode::kModel, 0U, operation};
}

} // namespace

common::Error Decoder::Initialize(const ModelOutputSpec &spec)
{
    initialized_ = false;
    mask_buffer_index_ = 0U;
    if (spec.count != 1U || spec.tensors[0].size_bytes != kMaskBytes * 2U ||
        static_memory_layout::kLayout.segmentation_mask[0].size() < kMaskBytes ||
        static_memory_layout::kLayout.segmentation_mask[1].size() < kMaskBytes) {
        return Invalid("segmentation.decoder.output_shape");
    }
    initialized_ = true;
    return {common::ErrorCode::kOk, 0U,
            "segmentation.decoder.initialize"};
}

common::Error Decoder::Decode(const InferenceCompletionContext &context,
                              ModelResult *result)
{
    if (!initialized_) {
        return {common::ErrorCode::kNotInitialized, 0U,
                "segmentation.decoder.decode"};
    }
    if (result == nullptr || context.outputs.count != 1U ||
        context.outputs.tensors[0].data == nullptr) {
        return {common::ErrorCode::kInvalidArgument, 0U,
                "segmentation.decoder.decode"};
    }

    const auto *logits = reinterpret_cast<const std::int8_t *>(
        context.outputs.tensors[0].data);
    const auto &mask_region =
        static_memory_layout::kLayout.segmentation_mask[mask_buffer_index_];
    auto *mask = reinterpret_cast<std::uint8_t *>(mask_region.address());
    std::uint32_t foreground_pixels = 0U;
    for (std::size_t i = 0U; i < kMaskBytes; ++i) {
        mask[i] = logits[2U * i + 1U] > logits[2U * i] ? 1U : 0U;
        foreground_pixels += mask[i];
    }

    result->kind = ModelKind::kSegmentation;
    result->segmentation_valid = true;
    result->segmentation.mask_address = mask_region.address();
    result->segmentation.mask_width = static_cast<std::uint16_t>(kMaskWidth);
    result->segmentation.mask_height = static_cast<std::uint16_t>(kMaskHeight);
    result->segmentation.mask_foreground_pixels = foreground_pixels;
    mask_buffer_index_ ^= 1U;
    return {common::ErrorCode::kOk, 0U, "segmentation.decoder.decode"};
}

} // namespace uai::ai::models::segmentation
