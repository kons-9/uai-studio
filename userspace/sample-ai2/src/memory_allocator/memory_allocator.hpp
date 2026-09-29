#ifndef UAI_AI_MEMORY_ALLOCATOR_HPP
#define UAI_AI_MEMORY_ALLOCATOR_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "common/error.hpp"
#include "buffer_layout/buffer_layout.hpp"
#include "buffer_layout/lease_pool.hpp"

namespace uai::ai::cache { class CacheDriver; }

namespace uai::ai::memory_allocator {

/* Board-specific capacities live beside the allocator. The arithmetic lives
 * in middleware and can be host-tested without the STM32 toolchain. */
inline constexpr buffer_layout::Config kConfig{
    800U, 480U, 2U,
    480U, 480U, 3U, 32U,
    {8192U, 60U * 60U * 18U, 30U * 30U * 18U, 384U * 16U},
    480U, 288U, 16U,
};
inline constexpr std::size_t kInferenceBufferCount = 5U;

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
    std::uint64_t lease_token = 0U;
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
    /* Arm only after the board has enabled PSRAM. Guard cache maintenance is
     * platform-owned; the layout/guard arithmetic remains middleware code. */
    common::Error ArmInferenceGuards(cache::CacheDriver &cache);
    common::Error CheckInferenceGuards(const InferenceFrame &frame) const;

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
    buffer_layout::LeasePool<kInferenceBufferCount> inference_leases_{};
    cache::CacheDriver *guard_cache_ = nullptr;
    bool initialized_ = false;
};

} // namespace uai::ai::memory_allocator

#endif
