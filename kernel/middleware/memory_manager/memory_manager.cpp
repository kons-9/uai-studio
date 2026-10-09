#include "memory_manager/memory_manager.hpp"

#include "memory_manager/memory_sizes.hpp"
#include "middleware/buffer/interrupt_guard.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"
#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/pipeline/frame_types.hpp"

namespace uai::ai::memory_manager {

namespace {

using StaticMemoryKey = static_memory_layout::Key;
using buffer::Buffer;
using buffer::BufferState;
using buffer::InterruptGuard;
using buffer::Region;
using pipeline::CaptureFrame;
using pipeline::DisplayBuffer;
using pipeline::InferenceFrame;

static_memory_layout::Region StaticRegion(StaticMemoryKey key)
{
    return static_memory_layout::Region::GetRegionFromKey(key);
}

std::uintptr_t CaptureAddress(std::uint8_t index)
{
    return StaticRegion(index == 0U ? StaticMemoryKey::kCapture0 : StaticMemoryKey::kCapture1).address();
}

std::uintptr_t InferenceAddress(std::size_t index)
{
    return static_memory_layout::Region::GetRegionFromKey(static_memory_layout::kInferenceRegionKeys[index]).address();
}

bool InferenceLeased(BufferState state)
{
    return state == BufferState::kReadyForAi || state == BufferState::kInUseByAi;
}

} // namespace

common::Error MemoryManager::ValidateLayout() const
{
    constexpr std::size_t region_count = static_cast<std::size_t>(StaticMemoryKey::kCount);
    static_memory_layout::AddressRange ranges[region_count]{};
    for (std::size_t i = 0U; i < region_count; ++i) {
        const auto region = StaticRegion(static_cast<StaticMemoryKey>(i));
        if (!region.is_valid() || (region.address() % kMemoryConfig.buffer_alignment) != 0U) {
            return {common::ErrorCode::kInvalidArgument};
        }
        ranges[i] = region.to_address_range();
    }
    for (std::size_t lhs = 0U; lhs < region_count; ++lhs) {
        for (std::size_t rhs = lhs + 1U; rhs < region_count; ++rhs) {
            if (ranges[lhs].overlaps(ranges[rhs])) {
                return {common::ErrorCode::kInvalidArgument};
            }
        }
    }

    if (kCaptureBufferBytes > StaticRegion(StaticMemoryKey::kCapture0).size()
        || kCaptureBufferBytes > StaticRegion(StaticMemoryKey::kCapture1).size()
        || kCaptureBufferBytes > StaticRegion(StaticMemoryKey::kDisplay0).size()
        || kCaptureBufferBytes > StaticRegion(StaticMemoryKey::kDisplay1).size()
        || kInferenceScratchBytes > StaticRegion(StaticMemoryKey::kInferenceScratch).size()) {
        return {common::ErrorCode::kInvalidArgument};
    }
    for (std::size_t i = 0U; i < kInferenceSourceBufferCount; ++i) {
        if (kInferenceSourceBytes
            > static_memory_layout::Region::GetRegionFromKey(static_memory_layout::kInferenceSourceRegionKeys[i])
                  .size()) {
            return {common::ErrorCode::kInvalidArgument};
        }
    }
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (kInferenceBufferBytes
            > static_memory_layout::Region::GetRegionFromKey(static_memory_layout::kInferenceRegionKeys[i]).size()) {
            return {common::ErrorCode::kInvalidArgument};
        }
    }
    return {};
}

common::Error MemoryManager::Initialize()
{
    if (initialized_) {
        return {common::ErrorCode::kAlreadyInitialized};
    }
    const common::Error status = ValidateLayout();
    if (!status.Ok())
        return status;

    display_[0].buffer = {
        StaticRegion(StaticMemoryKey::kDisplay0).address(), kCaptureBufferBytes, 0U, Region::kDisplay
    };
    display_[1].buffer = {
        StaticRegion(StaticMemoryKey::kDisplay1).address(), kCaptureBufferBytes, 1U, Region::kDisplay
    };
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        inference_[static_cast<std::uint8_t>(i)].buffer = {
            InferenceAddress(i), kInferenceBufferBytes, static_cast<std::uint8_t>(i), Region::kInference
        };
    }
    current_display_ = -1;
    pending_display_ = -1;
    capture_sequence_ = 0U;
    for (auto &generation : capture_generation_)
        generation = 0U;
    for (auto &sequence : inference_capture_sequence_)
        sequence = 0U;
    initialized_ = true;
    return {};
}

void MemoryManager::PopulateInferenceFrame(
    const InferencePool::Slot &slot,
    std::uint32_t sequence,
    bool from_pipe2,
    InferenceFrame *frame
) const
{
    const std::uint8_t index = slot.buffer.index;
    *frame = {};
    frame->buffer = slot.buffer;
    frame->source = {
        static_memory_layout::Region::GetRegionFromKey(static_memory_layout::kInferenceSourceRegionKeys[index])
            .address(),
        kInferenceSourceBytes,
        index,
        Region::kInference,
        kMemoryConfig.buffer_alignment
    };
    frame->scratch = {
        StaticRegion(StaticMemoryKey::kInferenceScratch).address(),
        kInferenceScratchBytes,
        index,
        Region::kInference,
        kMemoryConfig.buffer_alignment
    };
    frame->output_count = static_cast<std::uint8_t>(kMemoryConfig.model_output_bytes.size());
    frame->capture_sequence = sequence;
    frame->lease_token = slot.lease_token;
    frame->from_pipe2 = from_pipe2;

    std::uintptr_t output_address = slot.buffer.address + kInferenceOutputsOffset;
    for (std::size_t output = 0U; output < kMemoryConfig.model_output_bytes.size(); ++output) {
        const std::size_t output_size = kMemoryConfig.model_output_bytes[output];
        frame->outputs[output] = {
            output_address, output_size, index, Region::kInference, kMemoryConfig.buffer_alignment
        };
        output_address += kMemoryConfig.AlignUp(output_size);
    }
}

common::Error MemoryManager::LookupDisplay(
    const Buffer &buffer,
    DisplayPool::Slot **slot
)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (buffer.region != Region::kDisplay) {
        return {common::ErrorCode::kInvalidArgument};
    }
    *slot = display_.Find(buffer.index);
    if (*slot == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    if ((*slot)->buffer != buffer)
        return {common::ErrorCode::kOwnership};
    return {};
}

common::Error MemoryManager::LookupInference(
    const InferenceFrame &frame,
    InferencePool::Slot **slot
)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (frame.buffer.region != Region::kInference) {
        return {common::ErrorCode::kInvalidArgument};
    }
    *slot = inference_.Find(frame.buffer.index);
    if (*slot == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    if ((*slot)->buffer != frame.buffer || inference_capture_sequence_[frame.buffer.index] != frame.capture_sequence
        || frame.lease_token == 0U || frame.lease_token != (*slot)->lease_token) {
        return {common::ErrorCode::kOwnership};
    }
    return {};
}

common::Error MemoryManager::CaptureBuffer(
    std::uint8_t index,
    Buffer *buffer
) const
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (buffer == nullptr || index >= kCaptureBufferCount) {
        return {common::ErrorCode::kInvalidArgument};
    }
    *buffer = {CaptureAddress(index), kCaptureBufferBytes, index, Region::kCapture};
    return {};
}

common::Error MemoryManager::InferenceBuffer(
    std::uint8_t index,
    Buffer *buffer
) const
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    const auto *slot = inference_.Find(index);
    if (buffer == nullptr || slot == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    *buffer = slot->buffer;
    return {};
}

common::Error MemoryManager::InferenceDropBuffer(Buffer *buffer) const
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (buffer == nullptr)
        return {common::ErrorCode::kInvalidArgument};
    const auto region = StaticRegion(StaticMemoryKey::kPipe2Drop);
    *buffer = {region.address(), region.size(), 0U, Region::kInference};
    return {};
}

common::Error MemoryManager::CaptureBuffers(
    std::uintptr_t *first,
    std::uintptr_t *second
) const
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (first == nullptr || second == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    *first = CaptureAddress(0U);
    *second = CaptureAddress(1U);
    return {};
}

common::Error MemoryManager::InferenceBuffers(
    std::uintptr_t *buffers,
    std::size_t count
) const
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (buffers == nullptr || count < kInferenceBufferCount) {
        return {common::ErrorCode::kInvalidArgument};
    }
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        buffers[i] = InferenceAddress(i);
    }
    return {};
}

common::Error MemoryManager::ImportCompletedCapture(
    std::uintptr_t address,
    CaptureFrame *frame
)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (frame == nullptr)
        return {common::ErrorCode::kInvalidArgument};

    std::uint8_t index = 0U;
    if (address == CaptureAddress(1U)) {
        index = 1U;
    } else if (address != CaptureAddress(0U)) {
        return {common::ErrorCode::kInvalidArgument};
    }

    ++capture_sequence_;
    capture_generation_[index] = capture_sequence_;
    frame->buffer = {address, kCaptureBufferBytes, index, Region::kCapture};
    frame->sequence = capture_sequence_;
    return {};
}

common::Error MemoryManager::ValidateCaptureFrame(const CaptureFrame &frame) const
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (!frame || frame.buffer.region != Region::kCapture || frame.buffer.index >= kCaptureBufferCount) {
        return {common::ErrorCode::kInvalidArgument};
    }
    if (frame.buffer.address != CaptureAddress(frame.buffer.index) || frame.buffer.size != kCaptureBufferBytes
        || frame.sequence == 0U || capture_generation_[frame.buffer.index] != frame.sequence) {
        return {common::ErrorCode::kOwnership};
    }
    return {};
}

common::Error MemoryManager::AcquireDisplayBuffer(DisplayBuffer *buffer)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (buffer == nullptr)
        return {common::ErrorCode::kInvalidArgument};

    for (std::uint8_t i = 0U; i < kDisplayBufferCount; ++i) {
        if (static_cast<std::int8_t>(i) == current_display_ || static_cast<std::int8_t>(i) == pending_display_) {
            continue;
        }
        auto &slot = display_[i];
        if (slot.state != BufferState::kFree)
            continue;
        InterruptGuard guard;
        slot.state = BufferState::kFilling;
        buffer->buffer = slot.buffer;
        return {};
    }
    return {common::ErrorCode::kNoBuffer};
}

common::Error MemoryManager::CommitDisplayBuffer(const DisplayBuffer &buffer)
{
    DisplayPool::Slot *slot = nullptr;
    if (const common::Error status = LookupDisplay(buffer.buffer, &slot); !status.Ok())
        return status;
    if (slot->state != BufferState::kFilling) {
        return {common::ErrorCode::kInvalidState};
    }
    if (pending_display_ >= 0)
        return {common::ErrorCode::kNoBuffer};

    InterruptGuard guard;
    slot->state = BufferState::kReady;
    pending_display_ = static_cast<std::int8_t>(buffer.buffer.index);
    return {};
}

common::Error MemoryManager::CompleteDisplayHandoff()
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    if (pending_display_ < 0)
        return {};

    auto &pending = display_[static_cast<std::uint8_t>(pending_display_)];
    if (pending.state != BufferState::kReady) {
        return {common::ErrorCode::kInvalidState};
    }
    InterruptGuard guard;
    if (current_display_ >= 0 && current_display_ != pending_display_) {
        auto &current = display_[static_cast<std::uint8_t>(current_display_)];
        if (current.state != BufferState::kScanning) {
            return {common::ErrorCode::kInvalidState};
        }
        display_.Release(current);
    }
    pending.state = BufferState::kScanning;
    current_display_ = pending_display_;
    pending_display_ = -1;
    return {};
}

common::Error MemoryManager::ReleaseDisplayBuffer(const DisplayBuffer &buffer)
{
    DisplayPool::Slot *slot = nullptr;
    if (const common::Error status = LookupDisplay(buffer.buffer, &slot); !status.Ok())
        return status;
    if (slot->state != BufferState::kFilling) {
        return {common::ErrorCode::kInvalidState};
    }
    InterruptGuard guard;
    display_.Release(*slot);
    return {};
}

common::Error MemoryManager::AcquireInferenceBuffer(
    const CaptureFrame &capture,
    InferenceFrame *frame
)
{
    const common::Error capture_status = ValidateCaptureFrame(capture);
    if (!capture_status.Ok())
        return capture_status;
    if (frame == nullptr)
        return {common::ErrorCode::kInvalidArgument};

    InterruptGuard guard;
    auto *slot = inference_.FindFree();
    if (slot == nullptr)
        return {common::ErrorCode::kNoBuffer};
    inference_.Lease(*slot, BufferState::kReadyForAi);
    inference_capture_sequence_[slot->buffer.index] = capture.sequence;
    PopulateInferenceFrame(*slot, capture.sequence, false, frame);
    return {};
}

common::Error MemoryManager::ImportCompletedInference(
    std::uintptr_t address,
    std::uint32_t sequence,
    InferenceFrame *frame
)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    const auto *slot = inference_.FindByAddress(address);
    if (frame == nullptr || sequence == 0U || slot == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    if (slot->state != BufferState::kReadyForAi || inference_capture_sequence_[slot->buffer.index] != sequence) {
        return {common::ErrorCode::kOwnership};
    }
    PopulateInferenceFrame(*slot, sequence, true, frame);
    return {};
}

common::Error MemoryManager::ReserveCompletedInference(
    std::uintptr_t address,
    std::uint32_t sequence
)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    auto *slot = inference_.FindByAddress(address);
    if (address == 0U || sequence == 0U || slot == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    if (slot->state != BufferState::kFree)
        return {common::ErrorCode::kNoBuffer};

    InterruptGuard guard;
    inference_.Lease(*slot, BufferState::kReadyForAi);
    inference_capture_sequence_[slot->buffer.index] = sequence;
    return {};
}

common::Error MemoryManager::DropCompletedInference(
    std::uintptr_t address,
    std::uint32_t sequence
)
{
    if (!initialized_)
        return {common::ErrorCode::kNotInitialized};
    auto *slot = inference_.FindByAddress(address);
    if (address == 0U || sequence == 0U || slot == nullptr) {
        return {common::ErrorCode::kInvalidArgument};
    }
    if (slot->state != BufferState::kReadyForAi || inference_capture_sequence_[slot->buffer.index] != sequence) {
        return {common::ErrorCode::kOwnership};
    }
    InterruptGuard guard;
    inference_.Release(*slot);
    inference_capture_sequence_[slot->buffer.index] = 0U;
    return {};
}

bool MemoryManager::IsInferenceBufferFree(std::uintptr_t address) const
{
    if (!initialized_)
        return false;
    const auto *slot = inference_.FindByAddress(address);
    return slot != nullptr && slot->state == BufferState::kFree;
}

common::Error MemoryManager::ClaimInferenceBuffer(const InferenceFrame &frame)
{
    InferencePool::Slot *slot = nullptr;
    if (const common::Error status = LookupInference(frame, &slot); !status.Ok())
        return status;
    if (slot->state != BufferState::kReadyForAi) {
        return {common::ErrorCode::kInvalidState};
    }
    InterruptGuard guard;
    slot->state = BufferState::kInUseByAi;
    return {};
}

common::Error MemoryManager::ReleaseInferenceBuffer(const InferenceFrame &frame)
{
    InferencePool::Slot *slot = nullptr;
    if (const common::Error status = LookupInference(frame, &slot); !status.Ok())
        return status;
    if (!InferenceLeased(slot->state)) {
        return {common::ErrorCode::kInvalidState};
    }
    InterruptGuard guard;
    inference_.Release(*slot);
    inference_capture_sequence_[frame.buffer.index] = 0U;
    return {};
}

} // namespace uai::ai::memory_manager
