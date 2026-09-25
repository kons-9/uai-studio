#ifndef UAI_AI_MEMORY_MANAGER_HPP
#define UAI_AI_MEMORY_MANAGER_HPP

#include <cstddef>
#include <cstdint>

#include "common/error.hpp"

namespace uai::ai::memory_manager {

constexpr std::uint32_t kFrameWidth = 800U;
constexpr std::uint32_t kFrameHeight = 480U;
constexpr std::size_t kFrameBytes =
    static_cast<std::size_t>(kFrameWidth) * kFrameHeight * 2U;
#if defined(AI_MODEL_SEGMENTATION)
constexpr std::uint32_t kInferenceWidth = 320U;
constexpr std::uint32_t kInferenceHeight = 320U;
#else
constexpr std::uint32_t kInferenceWidth = 480U;
constexpr std::uint32_t kInferenceHeight = 480U;
#endif
constexpr std::size_t kInferenceFrameBytes =
    static_cast<std::size_t>(kInferenceWidth) * kInferenceHeight * 3U;
/* Keep the legacy copy-only path safe while Pipe2 uses the smaller RGB888
 * model buffer. The hardware only writes kInferenceFrameBytes bytes. */
constexpr std::size_t kInferenceBufferBytes =
    kFrameBytes > kInferenceFrameBytes ? kFrameBytes : kInferenceFrameBytes;
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
    std::uint32_t capture_sequence = 0U;
    bool from_pipe2 = false;

    explicit operator bool() const { return static_cast<bool>(buffer); }
};

struct Box {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t width = 0;
    std::int16_t height = 0;
    float confidence = 0.0F;
};

struct BoxSet {
    std::uint32_t count = 0U;
    std::uint32_t model_sequence = 0U;
    std::uint32_t capture_sequence = 0U;
    std::uintptr_t mask_address = 0U;
    std::uint16_t mask_width = 0U;
    std::uint16_t mask_height = 0U;
    Box boxes[kMaxBoxes]{};
};

class MemoryManager final {
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

    Slot display_[2]{};
    Slot inference_[2]{};
    std::int8_t current_display_ = -1;
    std::int8_t pending_display_ = -1;
    std::uint32_t capture_sequence_ = 0U;
    std::uint32_t capture_generation_[2]{};
    std::uint32_t inference_capture_sequence_[2]{};
    bool initialized_ = false;
};

} // namespace uai::ai::memory_manager

#endif
