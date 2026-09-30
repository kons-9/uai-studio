#ifndef UAI_AI_MEMORY_MANAGER_HPP
#define UAI_AI_MEMORY_MANAGER_HPP

#include "memory_manager/fixed_pool_allocator.hpp"
#include "memory_manager/memory_config.hpp"

namespace uai::ai::pipeline {
struct CaptureFrame;
struct DisplayBuffer;
struct InferenceFrame;
} // namespace uai::ai::pipeline

namespace uai::ai::memory_manager {

class MemoryManager final {
public:
    MemoryManager() = default;

    common::Error Initialize();

    common::Error Acquire(const memory_allocator::BufferRequest &request,
                          UniquePointer *pointer);
    common::Error AcquireShared(
        const memory_allocator::BufferRequest &request,
        SharedPointer *pointer);
    common::Error Validate(
        const PointerIdentity &identity) const;
    common::Error Validate(const UniquePointer &pointer) const;
    common::Error Validate(const SharedPointer &pointer) const;

    common::Error CaptureBuffer(std::uint8_t index,
                                memory_allocator::Buffer *buffer) const;
    common::Error InferenceBuffer(
        std::uint8_t index, memory_allocator::Buffer *buffer) const;
    common::Error InferenceDropBuffer(memory_allocator::Buffer *buffer) const;
    common::Error CaptureBuffers(std::uintptr_t *first,
                                std::uintptr_t *second) const;
    common::Error InferenceBuffers(std::uintptr_t *buffers,
                                   std::size_t count) const;
    common::Error ImportCompletedCapture(
        std::uintptr_t address, pipeline::CaptureFrame *frame);
    common::Error ImportCompletedInference(
        std::uintptr_t address, std::uint32_t sequence,
        pipeline::InferenceFrame *frame);
    common::Error ReserveCompletedInference(
        std::uintptr_t address, std::uint32_t sequence);
    common::Error DropCompletedInference(
        std::uintptr_t address, std::uint32_t sequence);
    bool IsInferenceBufferFree(std::uintptr_t address) const;
    common::Error ValidateCaptureFrame(
        const pipeline::CaptureFrame &frame) const;

    common::Error AcquireDisplayBuffer(
        pipeline::DisplayBuffer *buffer);
    common::Error CommitDisplayBuffer(
        const pipeline::DisplayBuffer &buffer);
    common::Error CompleteDisplayHandoff();
    common::Error ReleaseDisplayBuffer(
        const pipeline::DisplayBuffer &buffer);

    common::Error AcquireInferenceBuffer(
        const pipeline::CaptureFrame &capture,
        pipeline::InferenceFrame *frame);
    common::Error ClaimInferenceBuffer(
        const pipeline::InferenceFrame &frame);
    common::Error ReleaseInferenceBuffer(
        const pipeline::InferenceFrame &frame);

private:
    memory_allocator::FixedPoolAllocator allocator_{};
    static common::Error Make(common::ErrorCode code, std::uint32_t detail,
                              const char *operation);
    static bool SameBuffer(const memory_allocator::Buffer &lhs,
                           const memory_allocator::Buffer &rhs);
    static bool SameLease(
        const pipeline::InferenceFrame &frame,
        std::uint64_t lease_token);
    bool FindInferenceIndex(std::uintptr_t address,
                            std::uint8_t *index) const;
    common::Error PopulateInferenceFrame(
        std::uint8_t index, std::uint32_t sequence, bool from_pipe2,
        pipeline::InferenceFrame *frame) const;

    std::int8_t current_display_ = -1;
    std::int8_t pending_display_ = -1;
    std::uint32_t capture_sequence_ = 0U;
    std::uint32_t capture_generation_[memory_manager::kCaptureBufferCount]{};
    std::uint32_t inference_capture_sequence_[
        memory_manager::kInferenceBufferCount]{};
};

} // namespace uai::ai::memory_manager

#endif
