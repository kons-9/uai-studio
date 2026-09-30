#include "memory_manager/memory_manager.hpp"

#include "application/pipeline/frame_types.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "memory_manager/static_memory_layout.hpp"

#if defined(__arm__) || defined(__thumb__)
extern "C" {
#include "stm32n6xx_hal.h"
}
#endif

namespace uai::ai::memory_manager {

using namespace memory_allocator;
using namespace pipeline;

namespace {

using StaticMemoryKey = static_memory_layout::Key;

const static_memory_layout::Region &StaticRegion(StaticMemoryKey key)
{
    return static_memory_layout::GetRegion(key);
}

const static_memory_layout::Region &InferenceRegion(std::size_t index)
{
    switch (index) {
    case 0U:
        return StaticRegion(StaticMemoryKey::kInference0);
    case 1U:
        return StaticRegion(StaticMemoryKey::kInference1);
    case 2U:
        return StaticRegion(StaticMemoryKey::kInference2);
    default:
        return StaticRegion(StaticMemoryKey::kInference0);
    }
}

const static_memory_layout::Region &InferenceSourceRegion(std::size_t index)
{
    switch (index) {
    case 0U:
        return StaticRegion(StaticMemoryKey::kInferenceSource0);
    case 1U:
        return StaticRegion(StaticMemoryKey::kInferenceSource1);
    case 2U:
        return StaticRegion(StaticMemoryKey::kInferenceSource2);
    default:
        return StaticRegion(StaticMemoryKey::kInferenceSource0);
    }
}

} // namespace

common::Error MemoryManager::Make(common::ErrorCode code, std::uint32_t detail,
                          const char *operation)
{
    return {code, detail, operation};
}

bool MemoryManager::SameBuffer(const Buffer &lhs, const Buffer &rhs)
{
    return lhs.address == rhs.address && lhs.size == rhs.size &&
           lhs.index == rhs.index && lhs.region == rhs.region;
}

common::Error MemoryManager::PopulateInferenceFrame(
    std::uint8_t index, std::uint32_t sequence, bool from_pipe2,
    InferenceFrame *frame) const
{
    if (frame == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.inference.populate.null_output");
    }

    FixedPoolAllocator::SlotInfo slot{};
    const common::Error inspect_status =
        allocator_.Inspect(FixedPoolAllocator::kInferencePool, index, &slot);
    if (!inspect_status.Ok()) {
        return inspect_status;
    }

    frame->buffer = slot.buffer;
    frame->source = {
        InferenceSourceRegion(index).address(),
        kInferenceSourceBytes,
        index,
        Region::kInference,
        kMemoryConfig.buffer_alignment};
    frame->scratch = {
        StaticRegion(StaticMemoryKey::kInferenceScratch).address(),
        kInferenceScratchBytes,
        index,
        Region::kInference,
        kMemoryConfig.buffer_alignment};
    frame->output_count = static_cast<std::uint8_t>(
        kMemoryConfig.model_output_bytes.size());
    frame->capture_sequence = sequence;
    frame->lease_token = slot.lease_token;
    frame->from_pipe2 = from_pipe2;
    frame->source_valid = false;
    frame->input_prepared_by_cpu = false;
    frame->input_prepared = false;
    frame->prepared_model_kind_id = InferenceFrame::kUnknownModelKindId;
    frame->input_preparation_start_ms = 0U;
    frame->input_preparation_end_ms = 0U;
    frame->input_preparation_elapsed_ms = 0U;

    std::uintptr_t output_address =
        frame->buffer.address + kInferenceOutputsOffset;
    for (std::size_t output = 0U;
         output < kMemoryConfig.model_output_bytes.size(); ++output) {
        const std::size_t output_size = kMemoryConfig.model_output_bytes[output];
        frame->outputs[output] = {
            output_address, output_size, index, Region::kInference,
            kMemoryConfig.buffer_alignment};
        output_address += kMemoryConfig.AlignUp(output_size);
    }
    return Make(common::ErrorCode::kOk, index,
                "memory.inference.populate");
}


bool MemoryManager::SameLease(const InferenceFrame &frame,
                                              std::uint64_t lease_token)
{
    return frame.lease_token != 0U && frame.lease_token == lease_token;
}

bool MemoryManager::FindInferenceIndex(
    std::uintptr_t address, std::uint8_t *index) const
{
    if (index == nullptr) {
        return false;
    }
    for (std::uint8_t i = 0U;
         i < allocator_.PoolSize(FixedPoolAllocator::kInferencePool); ++i) {
        FixedPoolAllocator::SlotInfo slot{};
        if (!allocator_
                 .Inspect(FixedPoolAllocator::kInferencePool, i, &slot)
                 .Ok()) {
            continue;
        }
        if (address == slot.buffer.address) {
            *index = i;
            return true;
        }
    }
    return false;
}


common::Error MemoryManager::Initialize()
{
    const common::Error status = allocator_.Initialize();
    if (!status.Ok()) {
        return status;
    }
    current_display_ = -1;
    pending_display_ = -1;
    capture_sequence_ = 0U;
    capture_generation_[0] = 0U;
    capture_generation_[1] = 0U;
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        inference_capture_sequence_[i] = 0U;
    }
    return status;
}

common::Error MemoryManager::Acquire(
    const BufferRequest &request, UniquePointer *pointer)
{
    return allocator_.Acquire(request, pointer);
}

common::Error MemoryManager::AcquireShared(
    const BufferRequest &request, SharedPointer *pointer)
{
    return allocator_.AcquireShared(request, pointer);
}

common::Error MemoryManager::Validate(
    const PointerIdentity &identity) const
{
    return allocator_.Validate(identity);
}

common::Error MemoryManager::Validate(const UniquePointer &pointer) const
{
    return allocator_.Validate(pointer.identity());
}

common::Error MemoryManager::Validate(const SharedPointer &pointer) const
{
    return allocator_.Validate(pointer.identity());
}

common::Error MemoryManager::CaptureBuffer(
    std::uint8_t index, Buffer *buffer) const
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.capture.buffer.not_initialized");
    }
    if (buffer == nullptr || index >= kCaptureBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, index,
                    "memory.capture.buffer.invalid_argument");
    }
    const StaticMemoryKey key = index == 0U
                                    ? StaticMemoryKey::kCapture0
                                    : StaticMemoryKey::kCapture1;
    *buffer = {StaticRegion(key).address(), kCaptureBufferBytes, index,
               Region::kCapture};
    return Make(common::ErrorCode::kOk, index, "memory.capture.buffer");
}

common::Error MemoryManager::InferenceBuffer(
    std::uint8_t index, Buffer *buffer) const
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.buffer.not_initialized");
    }
    if (buffer == nullptr || index >= kInferenceBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, index,
                    "memory.inference.buffer.invalid_argument");
    }
    FixedPoolAllocator::SlotInfo slot{};
    const common::Error status =
        allocator_.Inspect(FixedPoolAllocator::kInferencePool, index, &slot);
    if (!status.Ok()) {
        return status;
    }
    *buffer = slot.buffer;
    return Make(common::ErrorCode::kOk, index, "memory.inference.buffer");
}

common::Error MemoryManager::InferenceDropBuffer(
    Buffer *buffer) const
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.drop_buffer.not_initialized");
    }
    if (buffer == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.inference.drop_buffer.null_output");
    }
    const auto &region = StaticRegion(StaticMemoryKey::kPipe2Drop);
    *buffer = {region.address(), region.size(), 0U, Region::kInference};
    return Make(common::ErrorCode::kOk, 0U,
                "memory.inference.drop_buffer");
}

common::Error MemoryManager::CaptureBuffers(std::uintptr_t *first,
                                    std::uintptr_t *second) const
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.capture_buffers.not_initialized");
    }
    if (first == nullptr || second == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.capture_buffers.null_output");
    }
    *first = StaticRegion(StaticMemoryKey::kCapture0).address();
    *second = StaticRegion(StaticMemoryKey::kCapture1).address();
    return Make(common::ErrorCode::kOk, 0U, "memory.capture_buffers");
}

common::Error MemoryManager::InferenceBuffers(std::uintptr_t *buffers,
                                                std::size_t count) const
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference_buffers.not_initialized");
    }
    if (buffers == nullptr || count < kInferenceBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.inference_buffers.null_output");
    }
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        buffers[i] = InferenceRegion(i).address();
    }
    return Make(common::ErrorCode::kOk, 0U, "memory.inference_buffers");
}

common::Error MemoryManager::ImportCompletedCapture(std::uintptr_t address,
                                            CaptureFrame *frame)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.capture.import.not_initialized");
    }
    if (frame == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.capture.import.null_output");
    }

    std::uint8_t index = 0U;
    if (address == StaticRegion(StaticMemoryKey::kCapture1).address()) {
        index = 1U;
    } else if (address != StaticRegion(StaticMemoryKey::kCapture0).address()) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.capture.import.unknown_address");
    }

    ++capture_sequence_;
    capture_generation_[index] = capture_sequence_;
    frame->buffer = {address, kCaptureBufferBytes, index,
                     Region::kCapture};
    frame->sequence = capture_sequence_;
    return Make(common::ErrorCode::kOk, frame->sequence, "memory.capture.import");
}

common::Error MemoryManager::ValidateCaptureFrame(const CaptureFrame &frame) const
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.capture.validate.not_initialized");
    }
    if (!frame || frame.buffer.region != Region::kCapture) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.capture.validate.wrong_region_or_empty");
    }
    if (frame.buffer.index >= 2U) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.capture.validate.bad_index");
    }

    const std::uintptr_t expected_address =
        frame.buffer.index == 0U
            ? StaticRegion(StaticMemoryKey::kCapture0).address()
            : StaticRegion(StaticMemoryKey::kCapture1).address();
    if (frame.buffer.address != expected_address ||
        frame.buffer.size != kCaptureBufferBytes) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.capture.validate.not_owned");
    }
    if (frame.sequence == 0U ||
        capture_generation_[frame.buffer.index] != frame.sequence) {
        return Make(common::ErrorCode::kOwnership, frame.sequence,
                    "memory.capture.validate.stale_generation");
    }
    return Make(common::ErrorCode::kOk, frame.buffer.index,
                "memory.capture.validate");
}

common::Error MemoryManager::AcquireDisplayBuffer(DisplayBuffer *buffer)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.display.acquire.not_initialized");
    }
    if (buffer == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.display.acquire.null_output");
    }

    for (std::uint8_t i = 0U; i < kDisplayBufferCount; ++i) {
        if (static_cast<std::int8_t>(i) == current_display_ ||
            static_cast<std::int8_t>(i) == pending_display_) {
            continue;
        }
        FixedPoolAllocator::SlotInfo slot{};
        const common::Error inspect_status =
            allocator_.Inspect(FixedPoolAllocator::kDisplayPool, i, &slot);
        if (!inspect_status.Ok()) {
            return inspect_status;
        }
        if (slot.state == BufferState::kFree) {
            const common::Error transition_status = allocator_.Transition(
                FixedPoolAllocator::kDisplayPool, i, BufferState::kFree,
                BufferState::kFilling);
            if (!transition_status.Ok()) {
                return transition_status;
            }
            buffer->buffer = slot.buffer;
            return Make(common::ErrorCode::kOk, i, "memory.display.acquire");
        }
    }
    return Make(common::ErrorCode::kNoBuffer, 0U, "memory.display.acquire.no_free_slot");
}

common::Error MemoryManager::CommitDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.display.commit.not_initialized");
    }
    if (buffer.buffer.region != Region::kDisplay) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(buffer.buffer.region),
                    "memory.display.commit.wrong_region");
    }
    if (buffer.buffer.index >= 2U) {
        return Make(common::ErrorCode::kInvalidArgument, buffer.buffer.index,
                    "memory.display.commit.bad_index");
    }

    FixedPoolAllocator::SlotInfo slot{};
    const common::Error inspect_status = allocator_.Inspect(
        FixedPoolAllocator::kDisplayPool, buffer.buffer.index, &slot);
    if (!inspect_status.Ok()) {
        return inspect_status;
    }
    if (!SameBuffer(slot.buffer, buffer.buffer)) {
        return Make(common::ErrorCode::kOwnership, buffer.buffer.index,
                    "memory.display.commit.not_owner");
    }
    if (slot.state != BufferState::kFilling) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.display.commit.expected_filling");
    }
    if (pending_display_ >= 0) {
        return Make(common::ErrorCode::kNoBuffer,
                    static_cast<std::uint32_t>(pending_display_),
                    "memory.display.commit.reload_pending");
    }
    const common::Error transition_status = allocator_.Transition(
        FixedPoolAllocator::kDisplayPool, buffer.buffer.index,
        BufferState::kFilling, BufferState::kReady);
    if (!transition_status.Ok()) {
        return transition_status;
    }
    pending_display_ = static_cast<std::int8_t>(buffer.buffer.index);
    return Make(common::ErrorCode::kOk, buffer.buffer.index, "memory.display.commit");
}

common::Error MemoryManager::CompleteDisplayHandoff()
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.display.handoff.not_initialized");
    }
    if (pending_display_ < 0) {
        return Make(common::ErrorCode::kOk, 0U, "memory.display.handoff.none");
    }

    const auto pending = static_cast<std::uint8_t>(pending_display_);
    FixedPoolAllocator::SlotInfo pending_slot{};
    const common::Error pending_status = allocator_.Inspect(
        FixedPoolAllocator::kDisplayPool, pending, &pending_slot);
    if (!pending_status.Ok()) {
        return pending_status;
    }
    if (pending_slot.state != BufferState::kReady) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(pending_slot.state),
                    "memory.display.handoff.pending_not_ready");
    }
    if (current_display_ >= 0 && current_display_ != pending_display_) {
        const common::Error release_status = allocator_.Transition(
            FixedPoolAllocator::kDisplayPool,
            static_cast<std::uint8_t>(current_display_),
            BufferState::kScanning, BufferState::kFree);
        if (!release_status.Ok()) {
            return release_status;
        }
    }
    const common::Error scan_status = allocator_.Transition(
        FixedPoolAllocator::kDisplayPool, pending, BufferState::kReady,
        BufferState::kScanning);
    if (!scan_status.Ok()) {
        return scan_status;
    }
    current_display_ = pending_display_;
    pending_display_ = -1;
    return Make(common::ErrorCode::kOk, pending, "memory.display.handoff");
}

common::Error MemoryManager::ReleaseDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.display.release.not_initialized");
    }
    if (buffer.buffer.region != Region::kDisplay) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(buffer.buffer.region),
                    "memory.display.release.wrong_region");
    }
    if (buffer.buffer.index >= 2U) {
        return Make(common::ErrorCode::kInvalidArgument, buffer.buffer.index,
                    "memory.display.release.bad_index");
    }

    FixedPoolAllocator::SlotInfo slot{};
    const common::Error inspect_status = allocator_.Inspect(
        FixedPoolAllocator::kDisplayPool, buffer.buffer.index, &slot);
    if (!inspect_status.Ok()) {
        return inspect_status;
    }
    if (!SameBuffer(slot.buffer, buffer.buffer)) {
        return Make(common::ErrorCode::kOwnership, buffer.buffer.index,
                    "memory.display.release.not_owner");
    }
    if (slot.state != BufferState::kFilling) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.display.release.expected_filling");
    }
    const common::Error transition_status = allocator_.Transition(
        FixedPoolAllocator::kDisplayPool, buffer.buffer.index,
        BufferState::kFilling, BufferState::kFree);
    if (!transition_status.Ok()) {
        return transition_status;
    }
    return Make(common::ErrorCode::kOk, buffer.buffer.index, "memory.display.release");
}

common::Error MemoryManager::AcquireInferenceBuffer(const CaptureFrame &capture,
                                            InferenceFrame *frame)
{
    const common::Error capture_status = ValidateCaptureFrame(capture);
    if (!capture_status.Ok()) {
        return capture_status;
    }
    if (frame == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.inference.acquire.null_output");
    }

    for (std::uint8_t i = 0U; i < kInferenceBufferCount; ++i) {
        FixedPoolAllocator::SlotInfo slot{};
        const common::Error inspect_status = allocator_.Inspect(
            FixedPoolAllocator::kInferencePool, i, &slot);
        if (!inspect_status.Ok()) {
            return inspect_status;
        }
        if (slot.state == BufferState::kFree) {
            std::uint64_t lease_token = 0U;
            const common::Error reserve_status =
                allocator_.ReserveSlot(
                    FixedPoolAllocator::kInferencePool, i,
                    BufferState::kFree, BufferState::kReadyForAi,
                    &lease_token);
            if (!reserve_status.Ok()) {
                return reserve_status;
            }
            inference_capture_sequence_[i] = capture.sequence;
            const common::Error populate_status =
                PopulateInferenceFrame(i, capture.sequence, false, frame);
            if (!populate_status.Ok()) {
                inference_capture_sequence_[i] = 0U;
                allocator_.Transition(
                    FixedPoolAllocator::kInferencePool, i,
                    BufferState::kReadyForAi, BufferState::kFree);
                return populate_status;
            }
            return Make(common::ErrorCode::kOk, i, "memory.inference.acquire");
        }
    }
    return Make(common::ErrorCode::kNoBuffer, 0U,
                "memory.inference.acquire.no_free_slot");
}

common::Error MemoryManager::ImportCompletedInference(std::uintptr_t address,
                                               std::uint32_t sequence,
                                               InferenceFrame *frame)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.import.not_initialized");
    }
    if (frame == nullptr || sequence == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, sequence,
                    "memory.inference.import.invalid_argument");
    }

    std::uint8_t index = 0U;
    if (!FindInferenceIndex(address, &index)) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.inference.import.unknown_address");
    }

    FixedPoolAllocator::SlotInfo slot{};
    const common::Error inspect_status = allocator_.Inspect(
        FixedPoolAllocator::kInferencePool, index, &slot);
    if (!inspect_status.Ok()) {
        return inspect_status;
    }
    if (slot.state != BufferState::kReadyForAi ||
        inference_capture_sequence_[index] != sequence) {
        return Make(common::ErrorCode::kOwnership,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.import.not_reserved");
    }
    const common::Error populate_status =
        PopulateInferenceFrame(index, sequence, true, frame);
    if (!populate_status.Ok()) {
        return populate_status;
    }
    return Make(common::ErrorCode::kOk, index, "memory.inference.import");
}

common::Error MemoryManager::ReserveCompletedInference(
    std::uintptr_t address, std::uint32_t sequence)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.reserve.not_initialized");
    }
    if (address == 0U || sequence == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, sequence,
                    "memory.inference.reserve.invalid_argument");
    }

    std::uint8_t index = 0U;
    if (!FindInferenceIndex(address, &index)) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.inference.reserve.unknown_address");
    }

    {
        FixedPoolAllocator::SlotInfo slot{};
        const common::Error inspect_status = allocator_.Inspect(
            FixedPoolAllocator::kInferencePool, index, &slot);
        if (!inspect_status.Ok()) {
            return inspect_status;
        }
        if (slot.state != BufferState::kFree) {
            return Make(common::ErrorCode::kNoBuffer,
                        static_cast<std::uint32_t>(slot.state),
                        "memory.inference.reserve.buffer_busy");
        }
        std::uint64_t lease_token = 0U;
        const common::Error reserve_status =
            allocator_.ReserveSlot(
                FixedPoolAllocator::kInferencePool, index,
                BufferState::kFree, BufferState::kReadyForAi,
                &lease_token);
        if (!reserve_status.Ok()) {
            return reserve_status;
        }
        inference_capture_sequence_[index] = sequence;
    }
    return Make(common::ErrorCode::kOk, index, "memory.inference.reserve");
}

common::Error MemoryManager::DropCompletedInference(
    std::uintptr_t address, std::uint32_t sequence)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.drop.not_initialized");
    }
    if (address == 0U || sequence == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, sequence,
                    "memory.inference.drop.invalid_argument");
    }

    std::uint8_t index = 0U;
    if (!FindInferenceIndex(address, &index)) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.inference.drop.unknown_address");
    }

    {
        FixedPoolAllocator::SlotInfo slot{};
        const common::Error inspect_status = allocator_.Inspect(
            FixedPoolAllocator::kInferencePool, index, &slot);
        if (!inspect_status.Ok()) {
            return inspect_status;
        }
        if (slot.state != BufferState::kReadyForAi ||
            inference_capture_sequence_[index] != sequence) {
            return Make(common::ErrorCode::kOwnership,
                        static_cast<std::uint32_t>(slot.state),
                        "memory.inference.drop.not_reserved");
        }
        const common::Error release_status = allocator_.Transition(
            FixedPoolAllocator::kInferencePool, index,
            BufferState::kReadyForAi, BufferState::kFree);
        if (!release_status.Ok()) {
            return release_status;
        }
        inference_capture_sequence_[index] = 0U;
    }
    return Make(common::ErrorCode::kOk, index, "memory.inference.drop");
}

bool MemoryManager::IsInferenceBufferFree(std::uintptr_t address) const
{
    if (!allocator_.IsInitialized()) {
        return false;
    }
    std::uint8_t index = 0U;
    bool found = false;
    for (std::uint8_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (address == InferenceRegion(i).address()) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    FixedPoolAllocator::SlotInfo slot{};
    if (!allocator_
             .Inspect(FixedPoolAllocator::kInferencePool, index, &slot)
             .Ok()) {
        return false;
    }
    return slot.state == BufferState::kFree;
}

common::Error MemoryManager::ClaimInferenceBuffer(const InferenceFrame &frame)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.claim.not_initialized");
    }
    if (frame.buffer.region != Region::kInference) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(frame.buffer.region),
                    "memory.inference.claim.wrong_region");
    }
    if (frame.buffer.index >= kInferenceBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.claim.bad_index");
    }

    FixedPoolAllocator::SlotInfo slot{};
    const common::Error inspect_status = allocator_.Inspect(
        FixedPoolAllocator::kInferencePool, frame.buffer.index, &slot);
    if (!inspect_status.Ok()) {
        return inspect_status;
    }
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        inference_capture_sequence_[frame.buffer.index] !=
            frame.capture_sequence ||
        !SameLease(frame, slot.lease_token)) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.claim.not_owner_or_stale");
    }
    if (slot.state != BufferState::kReadyForAi) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.claim.expected_ready");
    }
    const common::Error transition_status = allocator_.Transition(
        FixedPoolAllocator::kInferencePool, frame.buffer.index,
        BufferState::kReadyForAi, BufferState::kInUseByAi);
    if (!transition_status.Ok()) {
        return transition_status;
    }
    return Make(common::ErrorCode::kOk, frame.buffer.index,
                "memory.inference.claim");
}

common::Error MemoryManager::ReleaseInferenceBuffer(const InferenceFrame &frame)
{
    if (!allocator_.IsInitialized()) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.release.not_initialized");
    }
    if (frame.buffer.region != Region::kInference) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(frame.buffer.region),
                    "memory.inference.release.wrong_region");
    }
    if (frame.buffer.index >= kInferenceBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.release.bad_index");
    }

    FixedPoolAllocator::SlotInfo slot{};
    const common::Error inspect_status = allocator_.Inspect(
        FixedPoolAllocator::kInferencePool, frame.buffer.index, &slot);
    if (!inspect_status.Ok()) {
        return inspect_status;
    }
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        inference_capture_sequence_[frame.buffer.index] !=
            frame.capture_sequence ||
        !SameLease(frame, slot.lease_token)) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.release.not_owner_or_stale");
    }
    if (slot.state != BufferState::kInUseByAi &&
        slot.state != BufferState::kReadyForAi) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.release.not_in_use");
    }
    const common::Error release_status = allocator_.Transition(
        FixedPoolAllocator::kInferencePool, frame.buffer.index, slot.state,
        BufferState::kFree);
    if (!release_status.Ok()) {
        return release_status;
    }
    inference_capture_sequence_[frame.buffer.index] = 0U;
    return Make(common::ErrorCode::kOk, frame.buffer.index,
                "memory.inference.release");
}


} // namespace uai::ai::memory_manager
