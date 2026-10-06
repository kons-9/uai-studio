#ifndef UAI_AI_MEMORY_MANAGER_HPP
#define UAI_AI_MEMORY_MANAGER_HPP

#include <cstddef>
#include <cstdint>

#include "middleware/buffer/buffer_types.hpp"
#include "middleware/buffer/lease_pool.hpp"
#include "middleware/foundation/error.hpp"
#include "middleware/memory/generated/memory_config.hpp"

namespace uai::ai::pipeline {
struct CaptureFrame;
struct DisplayBuffer;
struct InferenceFrame;
} // namespace uai::ai::pipeline

namespace uai::ai::memory_manager {

/*
 * Owns the capture/display/inference buffers resolved from the generated
 * memory layout and drives their lifecycle. Display slots cycle through
 * free -> filling -> ready -> scanning; inference slots through
 * free -> ready-for-AI -> in-use-by-AI. The lease token and capture
 * sequence recorded on each InferenceFrame must match the slot before it
 * can be claimed or released.
 */
class MemoryManager final {
public:
    MemoryManager() = default;

    common::Error Initialize();

    common::Error CaptureBuffer(std::uint8_t index,
                                buffer::Buffer *buffer) const;
    common::Error InferenceBuffer(std::uint8_t index,
                                  buffer::Buffer *buffer) const;
    common::Error InferenceDropBuffer(buffer::Buffer *buffer) const;
    common::Error CaptureBuffers(std::uintptr_t *first,
                                 std::uintptr_t *second) const;
    common::Error InferenceBuffers(std::uintptr_t *buffers,
                                   std::size_t count) const;
    common::Error ImportCompletedCapture(std::uintptr_t address,
                                         pipeline::CaptureFrame *frame);
    common::Error ImportCompletedInference(std::uintptr_t address,
                                           std::uint32_t sequence,
                                           pipeline::InferenceFrame *frame);
    common::Error ReserveCompletedInference(std::uintptr_t address,
                                            std::uint32_t sequence);
    common::Error DropCompletedInference(std::uintptr_t address,
                                         std::uint32_t sequence);
    bool IsInferenceBufferFree(std::uintptr_t address) const;
    common::Error ValidateCaptureFrame(
        const pipeline::CaptureFrame &frame) const;

    common::Error AcquireDisplayBuffer(pipeline::DisplayBuffer *buffer);
    common::Error CommitDisplayBuffer(const pipeline::DisplayBuffer &buffer);
    common::Error CompleteDisplayHandoff();
    common::Error ReleaseDisplayBuffer(const pipeline::DisplayBuffer &buffer);

    common::Error AcquireInferenceBuffer(const pipeline::CaptureFrame &capture,
                                         pipeline::InferenceFrame *frame);
    common::Error ClaimInferenceBuffer(const pipeline::InferenceFrame &frame);
    common::Error ReleaseInferenceBuffer(const pipeline::InferenceFrame &frame);

private:
    using DisplayPool = buffer::LeasePool<kDisplayBufferCount>;
    using InferencePool = buffer::LeasePool<kInferenceBufferCount>;

    common::Error ValidateLayout() const;
    void PopulateInferenceFrame(const InferencePool::Slot &slot,
                                std::uint32_t sequence, bool from_pipe2,
                                pipeline::InferenceFrame *frame) const;
    common::Error LookupDisplay(const buffer::Buffer &buffer,
                                DisplayPool::Slot **slot);
    common::Error LookupInference(const pipeline::InferenceFrame &frame,
                                  InferencePool::Slot **slot);

    bool initialized_ = false;
    DisplayPool display_{};
    InferencePool inference_{};
    std::int8_t current_display_ = -1;
    std::int8_t pending_display_ = -1;
    std::uint32_t capture_sequence_ = 0U;
    std::uint32_t capture_generation_[kCaptureBufferCount]{};
    std::uint32_t inference_capture_sequence_[kInferenceBufferCount]{};
};

} // namespace uai::ai::memory_manager

#endif
