#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::memory_allocator {

/* Allocation sizing and policy live here. The physical memory map is provided
 * separately by static_memory_layout from the linker script. Derived sizes
 * stay constexpr, but are calculated from the values that describe the
 * allocation policy. */
struct MemoryAllocatorConfig {
    std::uint32_t frame_width = 800U;
    std::uint32_t frame_height = 480U;
    std::uint32_t frame_bytes_per_pixel = 2U;

    /* Runtime model switching keeps a small pool of fixed-size Pipe2/NPU
     * slots alive. Allocate for the largest input/output in the model set. */
    std::uint32_t inference_width = 480U;
    std::uint32_t inference_height = 480U;
    std::uint32_t inference_bytes_per_pixel = 3U;
    std::size_t buffer_alignment = 32U;

    /* Output slots are shared by the registered models.  Slot zero must fit
     * the largest first output (face: 8192 bytes); segmentation now exposes
     * its native 20x20x2 logits (800 bytes) after its final resize is cut. */
    std::array<std::size_t, 4U> model_output_bytes = {
        8192U, 60U * 60U * 18U, 30U * 30U * 18U, 384U * 16U};

    /* Dynamic switching uses the fixed person-sized Pipe2 image as a source
     * for smaller model tensors. Scratch capacity is shared because inference
     * is serialized. */
    std::uint32_t inference_source_width = 480U;
    std::uint32_t inference_source_height = 288U;
    std::size_t max_boxes = 16U;

    constexpr std::size_t AlignUp(std::size_t value) const
    {
        return (value + buffer_alignment - 1U) / buffer_alignment *
               buffer_alignment;
    }

    constexpr std::size_t frame_bytes() const
    {
        return static_cast<std::size_t>(frame_width) * frame_height *
               frame_bytes_per_pixel;
    }

    constexpr std::size_t inference_frame_bytes() const
    {
        return static_cast<std::size_t>(inference_width) * inference_height *
               inference_bytes_per_pixel;
    }

    constexpr std::size_t inference_scratch_bytes() const
    {
        return static_cast<std::size_t>(inference_source_width) *
               inference_source_height * inference_bytes_per_pixel;
    }

    constexpr std::size_t inference_source_bytes() const
    {
        return inference_frame_bytes();
    }

    constexpr std::size_t inference_outputs_offset() const
    {
        return AlignUp(inference_frame_bytes());
    }

    constexpr std::size_t inference_output_storage_bytes() const
    {
        std::size_t total = 0U;
        for (const std::size_t output_bytes : model_output_bytes) {
            total += AlignUp(output_bytes);
        }
        return total;
    }

    constexpr std::size_t inference_buffer_bytes() const
    {
        return AlignUp(inference_outputs_offset() +
                       inference_output_storage_bytes());
    }
};

inline constexpr MemoryAllocatorConfig kConfig{};
inline constexpr std::size_t kInferenceBufferCount = 3U;

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
    std::size_t alignment = kConfig.buffer_alignment;

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
    static constexpr std::uint8_t kUnknownModelKindId = 0xFFU;

    Buffer buffer{};
    Buffer source{};
    Buffer scratch{};
    Buffer outputs[kConfig.model_output_bytes.size()]{};
    std::uint8_t output_count = 0U;
    std::uint32_t capture_sequence = 0U;
    bool from_pipe2 = false;
    bool source_valid = false;
    bool input_prepared_by_cpu = false;
    bool input_prepared = false;
    std::uint8_t prepared_model_kind_id = kUnknownModelKindId;
    std::uint32_t input_preparation_start_ms = 0U;
    std::uint32_t input_preparation_end_ms = 0U;
    std::uint32_t input_preparation_elapsed_ms = 0U;

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
    Box boxes[kConfig.max_boxes]{};
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
    common::Error InferenceBuffers(std::uintptr_t *buffers,
                                   std::size_t count) const;
    common::Error ImportCompletedCapture(std::uintptr_t address,
                                         CaptureFrame *frame);
    common::Error ImportCompletedInference(std::uintptr_t address,
                                           std::uint32_t sequence,
                                           InferenceFrame *frame);
    /* Reserve the completed Pipe2 buffer from the ISR before another DMA
     * target is selected.  This prevents a delayed camera task from reading
     * an image that has already been overwritten. */
    common::Error ReserveCompletedInference(std::uintptr_t address,
                                            std::uint32_t sequence);
    common::Error DropCompletedInference(std::uintptr_t address,
                                         std::uint32_t sequence);
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
    Slot inference_[kInferenceBufferCount]{};
    std::int8_t current_display_ = -1;
    std::int8_t pending_display_ = -1;
    std::uint32_t capture_sequence_ = 0U;
    std::uint32_t capture_generation_[2]{};
    std::uint32_t inference_capture_sequence_[kInferenceBufferCount]{};
    bool initialized_ = false;
};

} // namespace uai::ai::memory_allocator
