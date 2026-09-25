#include "memory_manager/memory_manager.hpp"

namespace uai::ai::memory_manager {
namespace {

constexpr std::uintptr_t kCapture0 = 0x91000000UL;
constexpr std::uintptr_t kCapture1 = 0x91100000UL;
constexpr std::uintptr_t kDisplay0 = 0x91200000UL;
constexpr std::uintptr_t kDisplay1 = 0x91300000UL;
constexpr std::uintptr_t kInference0 = 0x91400000UL;
constexpr std::uintptr_t kInference1 = 0x91500000UL;

} // namespace

using common::ErrorCode;
using Error = common::Error;

Error MemoryManager::Make(ErrorCode code, std::uint32_t detail,
                          const char *operation)
{
    return {code, detail, operation};
}

bool MemoryManager::SameBuffer(const Buffer &lhs, const Buffer &rhs)
{
    return lhs.address == rhs.address && lhs.size == rhs.size &&
           lhs.index == rhs.index && lhs.region == rhs.region;
}

Error MemoryManager::Initialize()
{
    if (initialized_) {
        return Make(ErrorCode::kAlreadyInitialized, 0U, "memory.initialize");
    }

    display_[0].buffer = {kDisplay0, kFrameBytes, 0U, Region::kDisplay};
    display_[1].buffer = {kDisplay1, kFrameBytes, 1U, Region::kDisplay};
    inference_[0].buffer = {kInference0, kFrameBytes, 0U, Region::kInference};
    inference_[1].buffer = {kInference1, kFrameBytes, 1U, Region::kInference};
    current_display_ = -1;
    pending_display_ = -1;
    capture_sequence_ = 0U;
    capture_generation_[0] = 0U;
    capture_generation_[1] = 0U;
    inference_capture_sequence_[0] = 0U;
    inference_capture_sequence_[1] = 0U;
    initialized_ = true;
    return Make(ErrorCode::kOk, 0U, "memory.initialize");
}

Error MemoryManager::CaptureBuffers(std::uintptr_t *first,
                                    std::uintptr_t *second) const
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.capture_buffers.not_initialized");
    }
    if (first == nullptr || second == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.capture_buffers.null_output");
    }
    *first = kCapture0;
    *second = kCapture1;
    return Make(ErrorCode::kOk, 0U, "memory.capture_buffers");
}

Error MemoryManager::ImportCompletedCapture(std::uintptr_t address,
                                            CaptureFrame *frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.capture.import.not_initialized");
    }
    if (frame == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.capture.import.null_output");
    }

    std::uint8_t index = 0U;
    if (address == kCapture1) {
        index = 1U;
    } else if (address != kCapture0) {
        return Make(ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.capture.import.unknown_address");
    }

    ++capture_sequence_;
    capture_generation_[index] = capture_sequence_;
    frame->buffer = {address, kFrameBytes, index, Region::kCapture};
    frame->sequence = capture_sequence_;
    return Make(ErrorCode::kOk, frame->sequence, "memory.capture.import");
}

Error MemoryManager::ValidateCaptureFrame(const CaptureFrame &frame) const
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.capture.validate.not_initialized");
    }
    if (!frame || frame.buffer.region != Region::kCapture) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.capture.validate.wrong_region_or_empty");
    }
    if (frame.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.capture.validate.bad_index");
    }

    const std::uintptr_t expected_address =
        frame.buffer.index == 0U ? kCapture0 : kCapture1;
    if (frame.buffer.address != expected_address ||
        frame.buffer.size != kFrameBytes) {
        return Make(ErrorCode::kOwnership, frame.buffer.index,
                    "memory.capture.validate.not_owned");
    }
    if (frame.sequence == 0U ||
        capture_generation_[frame.buffer.index] != frame.sequence) {
        return Make(ErrorCode::kOwnership, frame.sequence,
                    "memory.capture.validate.stale_generation");
    }
    return Make(ErrorCode::kOk, frame.buffer.index,
                "memory.capture.validate");
}

Error MemoryManager::AcquireDisplayBuffer(DisplayBuffer *buffer)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.display.acquire.not_initialized");
    }
    if (buffer == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.display.acquire.null_output");
    }

    for (std::uint8_t i = 0U; i < 2U; ++i) {
        if (static_cast<std::int8_t>(i) == current_display_ ||
            static_cast<std::int8_t>(i) == pending_display_) {
            continue;
        }
        if (display_[i].state == BufferState::kFree) {
            display_[i].state = BufferState::kFilling;
            buffer->buffer = display_[i].buffer;
            return Make(ErrorCode::kOk, i, "memory.display.acquire");
        }
    }
    return Make(ErrorCode::kNoBuffer, 0U, "memory.display.acquire.no_free_slot");
}

Error MemoryManager::CommitDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.display.commit.not_initialized");
    }
    if (buffer.buffer.region != Region::kDisplay) {
        return Make(ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(buffer.buffer.region),
                    "memory.display.commit.wrong_region");
    }
    if (buffer.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, buffer.buffer.index,
                    "memory.display.commit.bad_index");
    }

    Slot &slot = display_[buffer.buffer.index];
    if (!SameBuffer(slot.buffer, buffer.buffer)) {
        return Make(ErrorCode::kOwnership, buffer.buffer.index,
                    "memory.display.commit.not_owner");
    }
    if (slot.state != BufferState::kFilling) {
        return Make(ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.display.commit.expected_filling");
    }
    if (pending_display_ >= 0) {
        return Make(ErrorCode::kNoBuffer,
                    static_cast<std::uint32_t>(pending_display_),
                    "memory.display.commit.reload_pending");
    }
    slot.state = BufferState::kReady;
    pending_display_ = static_cast<std::int8_t>(buffer.buffer.index);
    return Make(ErrorCode::kOk, buffer.buffer.index, "memory.display.commit");
}

Error MemoryManager::CompleteDisplayHandoff()
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.display.handoff.not_initialized");
    }
    if (pending_display_ < 0) {
        return Make(ErrorCode::kOk, 0U, "memory.display.handoff.none");
    }

    const auto pending = static_cast<std::uint8_t>(pending_display_);
    if (display_[pending].state != BufferState::kReady) {
        return Make(ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(display_[pending].state),
                    "memory.display.handoff.pending_not_ready");
    }
    if (current_display_ >= 0 && current_display_ != pending_display_) {
        display_[static_cast<std::uint8_t>(current_display_)].state =
            BufferState::kFree;
    }
    display_[pending].state = BufferState::kScanning;
    current_display_ = pending_display_;
    pending_display_ = -1;
    return Make(ErrorCode::kOk, pending, "memory.display.handoff");
}

Error MemoryManager::ReleaseDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.display.release.not_initialized");
    }
    if (buffer.buffer.region != Region::kDisplay) {
        return Make(ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(buffer.buffer.region),
                    "memory.display.release.wrong_region");
    }
    if (buffer.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, buffer.buffer.index,
                    "memory.display.release.bad_index");
    }

    Slot &slot = display_[buffer.buffer.index];
    if (!SameBuffer(slot.buffer, buffer.buffer)) {
        return Make(ErrorCode::kOwnership, buffer.buffer.index,
                    "memory.display.release.not_owner");
    }
    if (slot.state != BufferState::kFilling) {
        return Make(ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.display.release.expected_filling");
    }
    slot.state = BufferState::kFree;
    return Make(ErrorCode::kOk, buffer.buffer.index, "memory.display.release");
}

Error MemoryManager::AcquireInferenceBuffer(const CaptureFrame &capture,
                                            InferenceFrame *frame)
{
    const Error capture_status = ValidateCaptureFrame(capture);
    if (!capture_status.Ok()) {
        return capture_status;
    }
    if (frame == nullptr) {
        return Make(ErrorCode::kInvalidArgument, 0U,
                    "memory.inference.acquire.null_output");
    }

    for (std::uint8_t i = 0U; i < 2U; ++i) {
        if (inference_[i].state == BufferState::kFree) {
            inference_[i].state = BufferState::kReadyForAi;
            inference_capture_sequence_[i] = capture.sequence;
            frame->buffer = inference_[i].buffer;
            frame->capture_sequence = capture.sequence;
            return Make(ErrorCode::kOk, i, "memory.inference.acquire");
        }
    }
    return Make(ErrorCode::kNoBuffer, 0U,
                "memory.inference.acquire.no_free_slot");
}

Error MemoryManager::ClaimInferenceBuffer(const InferenceFrame &frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.inference.claim.not_initialized");
    }
    if (frame.buffer.region != Region::kInference) {
        return Make(ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(frame.buffer.region),
                    "memory.inference.claim.wrong_region");
    }
    if (frame.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.claim.bad_index");
    }

    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        inference_capture_sequence_[frame.buffer.index] !=
            frame.capture_sequence) {
        return Make(ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.claim.not_owner_or_stale");
    }
    if (slot.state != BufferState::kReadyForAi) {
        return Make(ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.claim.expected_ready");
    }
    slot.state = BufferState::kInUseByAi;
    return Make(ErrorCode::kOk, frame.buffer.index,
                "memory.inference.claim");
}

Error MemoryManager::ReleaseInferenceBuffer(const InferenceFrame &frame)
{
    if (!initialized_) {
        return Make(ErrorCode::kNotInitialized, 0U,
                    "memory.inference.release.not_initialized");
    }
    if (frame.buffer.region != Region::kInference) {
        return Make(ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(frame.buffer.region),
                    "memory.inference.release.wrong_region");
    }
    if (frame.buffer.index >= 2U) {
        return Make(ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.release.bad_index");
    }

    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        inference_capture_sequence_[frame.buffer.index] !=
            frame.capture_sequence) {
        return Make(ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.release.not_owner_or_stale");
    }
    if (slot.state != BufferState::kInUseByAi &&
        slot.state != BufferState::kReadyForAi) {
        return Make(ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.release.not_in_use");
    }
    slot.state = BufferState::kFree;
    inference_capture_sequence_[frame.buffer.index] = 0U;
    return Make(ErrorCode::kOk, frame.buffer.index,
                "memory.inference.release");
}

} // namespace uai::ai::memory_manager
