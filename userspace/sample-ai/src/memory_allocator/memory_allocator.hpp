#ifndef UAI_AI_MEMORY_ALLOCATOR_HPP
#define UAI_AI_MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::memory_allocator {

constexpr std::uint32_t kFrameWidth = 800U;
constexpr std::uint32_t kFrameHeight = 480U;
constexpr std::size_t kFrameBytes =
    static_cast<std::size_t>(kFrameWidth) * kFrameHeight * 2U;
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
/* Runtime model switching keeps one pair of fixed-size Pipe2/NPU slots alive.
 * Allocate for the largest input/output in the model set; each active model
 * uses only the prefix and output slots described by its runtime descriptor. */
constexpr std::uint32_t kInferenceWidth = 480U;
constexpr std::uint32_t kInferenceHeight = 480U;
#elif defined(AI_MODEL_SEGMENTATION)
constexpr std::uint32_t kInferenceWidth = 320U;
constexpr std::uint32_t kInferenceHeight = 320U;
#elif defined(AI_MODEL_FACE)
constexpr std::uint32_t kInferenceWidth = 128U;
constexpr std::uint32_t kInferenceHeight = 128U;
#else
constexpr std::uint32_t kInferenceWidth = 480U;
constexpr std::uint32_t kInferenceHeight = 480U;
#endif
constexpr std::size_t kInferenceFrameBytes =
    static_cast<std::size_t>(kInferenceWidth) * kInferenceHeight * 3U;
constexpr std::size_t kBufferAlignment = 32U;
constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

constexpr std::size_t kMaxModelOutputs = 4U;
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
constexpr std::size_t kModelOutputCount = 4U;
constexpr std::size_t kModelOutputBytes[kMaxModelOutputs] = {
    320U * 320U * 2U, 60U * 60U * 18U, 30U * 30U * 18U, 384U * 16U};
#elif defined(AI_MODEL_SEGMENTATION)
constexpr std::size_t kModelOutputCount = 1U;
constexpr std::size_t kModelOutputBytes[kMaxModelOutputs] = {
    320U * 320U * 2U, 0U, 0U, 0U};
#elif defined(AI_MODEL_FACE)
constexpr std::size_t kModelOutputCount = 4U;
constexpr std::size_t kModelOutputBytes[kMaxModelOutputs] = {
    512U * 16U, 512U, 384U, 384U * 16U};
#else
constexpr std::size_t kModelOutputCount = 3U;
constexpr std::size_t kModelOutputBytes[kMaxModelOutputs] = {
    15U * 15U * 18U, 60U * 60U * 18U, 30U * 30U * 18U, 0U};
#endif

/* Dynamic switching keeps the fixed person-sized Pipe2 image as a source for
 * the smaller segmentation and face tensors. The scratch area is a shared
 * allocator-owned region because inference is serialized; keeping it outside
 * the two 1-MB inference slots avoids overlap with the next DMA buffer. */
#if defined(AI_DYNAMIC_MODEL_SWITCHING)
constexpr std::uint32_t kInferenceSourceWidth = 480U;
constexpr std::uint32_t kInferenceSourceHeight = 288U;
constexpr std::size_t kInferenceScratchBytes =
    static_cast<std::size_t>(kInferenceSourceWidth) *
    kInferenceSourceHeight * 3U;
/* 0x91600000-0x919FFFFF is reserved by the camera raw-dump diagnostic path. */
constexpr std::uintptr_t kInferenceScratchAddress = 0x91C00000UL;
#else
constexpr std::uint32_t kInferenceSourceWidth = 0U;
constexpr std::uint32_t kInferenceSourceHeight = 0U;
constexpr std::size_t kInferenceScratchBytes = 0U;
constexpr std::uintptr_t kInferenceScratchAddress = 0U;
#endif
constexpr std::size_t kInferenceOutputsOffset =
    AlignUp(kInferenceFrameBytes, kBufferAlignment);
constexpr std::size_t kInferenceOutputStorageBytes =
    AlignUp(kModelOutputBytes[0], kBufferAlignment) +
    AlignUp(kModelOutputBytes[1], kBufferAlignment) +
    AlignUp(kModelOutputBytes[2], kBufferAlignment) +
    AlignUp(kModelOutputBytes[3], kBufferAlignment);
constexpr std::size_t kInferenceBufferBytes = AlignUp(
    kInferenceOutputsOffset + kInferenceOutputStorageBytes,
    kBufferAlignment);
constexpr std::size_t kMaxBoxes = 16U;

enum class Region : std::uint8_t {
    kCapture,
    kDisplay,
    kInference,
};

enum class BufferState : std::uint8_t {
    kFree,
    kFilling,
    kReady,
    kScanning,
    kReadyForAi,
    kInUseByAi,
};

struct Buffer {
    std::uintptr_t address = 0U;
    std::size_t size = 0U;
    std::uint8_t index = 0U;
    Region region = Region::kCapture;
    std::size_t alignment = kBufferAlignment;

    explicit operator bool() const { return address != 0U && size != 0U; }
};

struct CaptureFrame {
    Buffer buffer{};
    std::uint32_t sequence = 0U;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

struct DisplayBuffer {
    Buffer buffer{};

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

struct InferenceFrame {
    Buffer buffer{};
    Buffer scratch{};
    Buffer outputs[kMaxModelOutputs]{};
    std::uint8_t output_count = 0U;
    std::uint32_t capture_sequence = 0U;
    bool from_pipe2 = false;
    bool input_prepared_by_cpu = false;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

struct Box {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    float confidence = 0.0F;
};

struct DetectionSet {
    std::uint32_t count = 0U;
    Box boxes[kMaxBoxes]{};
};

struct SegmentationSet {
    std::uintptr_t mask_address = 0U;
    std::uint16_t mask_width = 0U;
    std::uint16_t mask_height = 0U;
    std::uint32_t mask_foreground_pixels = 0U;
};

struct BoxSet {
    std::uint32_t model_sequence = 0U;
    std::uint32_t capture_sequence = 0U;
    bool person_valid = false;
    bool face_valid = false;
    bool segmentation_valid = false;
    DetectionSet person{};
    DetectionSet face{};
    SegmentationSet segmentation{};
};

class MemoryAllocator final {
public:
    common::Error Initialize();

    common::Error CaptureBuffers(std::uintptr_t *first,
                                 std::uintptr_t *second) const;
    common::Error InferenceBuffers(std::uintptr_t *first,
                                   std::uintptr_t *second) const;
    common::Error ImportCompletedCapture(std::uintptr_t address,
                                         CaptureFrame *frame);
    common::Error ImportCompletedInference(std::uintptr_t address,
                                           std::uint32_t sequence,
                                           InferenceFrame *frame);
    bool IsInferenceBufferFree(std::uintptr_t address) const;
    common::Error ValidateCaptureFrame(const CaptureFrame &frame) const;

    common::Error AcquireDisplayBuffer(DisplayBuffer *buffer);
    common::Error CommitDisplayBuffer(const DisplayBuffer &buffer);
    common::Error CompleteDisplayHandoff();
    common::Error ReleaseDisplayBuffer(const DisplayBuffer &buffer);

    common::Error AcquireInferenceBuffer(const CaptureFrame &capture,
                                         InferenceFrame *frame);
    common::Error ClaimInferenceBuffer(const InferenceFrame &frame);
    common::Error ReleaseInferenceBuffer(const InferenceFrame &frame);

private:
    struct Slot {
        Buffer buffer{};
        BufferState state = BufferState::kFree;
    };

    static common::Error Make(common::ErrorCode code, std::uint32_t detail,
                              const char *operation);
    static bool SameBuffer(const Buffer &lhs, const Buffer &rhs);
    void PopulateInferenceFrame(std::uint8_t index,
                                std::uint32_t sequence,
                                bool from_pipe2,
                                InferenceFrame *frame) const;

    Slot display_[2]{};
    Slot inference_[2]{};
    std::int8_t current_display_ = -1;
    std::int8_t pending_display_ = -1;
    std::uint32_t capture_sequence_ = 0U;
    std::uint32_t capture_generation_[2]{};
    std::uint32_t inference_capture_sequence_[2]{};
    bool initialized_ = false;
};

} // namespace uai::ai::memory_allocator

#endif
