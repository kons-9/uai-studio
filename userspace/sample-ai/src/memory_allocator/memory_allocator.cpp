#include "memory_allocator/memory_allocator.hpp"

#include "static_memory_layout/static_memory_layout.hpp"

namespace uai::ai::memory_allocator {

common::Error MemoryAllocator::Make(common::ErrorCode code, std::uint32_t detail,
                          const char *operation)
{
    return {code, detail, operation};
}

bool MemoryAllocator::SameBuffer(const Buffer &lhs, const Buffer &rhs)
{
    return lhs.address == rhs.address && lhs.size == rhs.size &&
           lhs.index == rhs.index && lhs.region == rhs.region;
}

void MemoryAllocator::PopulateInferenceFrame(std::uint8_t index,
                                             std::uint32_t sequence,
                                             bool from_pipe2,
                                             InferenceFrame *frame) const
{
    frame->buffer = inference_[index].buffer;
    frame->scratch = {
        static_memory_layout::kLayout.inference_scratch.address(),
        kConfig.inference_scratch_bytes(),
        index,
        Region::kInference,
        kConfig.buffer_alignment};
    frame->output_count = static_cast<std::uint8_t>(
        kConfig.model_output_bytes.size());
    frame->capture_sequence = sequence;
    frame->from_pipe2 = from_pipe2;
    frame->input_prepared_by_cpu = false;

    std::uintptr_t output_address =
        frame->buffer.address + kConfig.inference_outputs_offset();
    for (std::size_t output = 0U;
         output < kConfig.model_output_bytes.size(); ++output) {
        const std::size_t output_size = kConfig.model_output_bytes[output];
        frame->outputs[output] = {
            output_address, output_size, index, Region::kInference,
            kConfig.buffer_alignment};
        output_address += kConfig.AlignUp(output_size);
    }
}

common::Error MemoryAllocator::Initialize()
{
    if (initialized_) {
        return Make(common::ErrorCode::kAlreadyInitialized, 0U, "memory.initialize");
    }

    const auto &layout = static_memory_layout::kLayout;
    const std::size_t frame_bytes = kConfig.frame_bytes();
    const std::size_t inference_bytes = kConfig.inference_buffer_bytes();
    const std::size_t scratch_bytes = kConfig.inference_scratch_bytes();
    if (frame_bytes > layout.capture[0].size() ||
        frame_bytes > layout.capture[1].size() ||
        frame_bytes > layout.display[0].size() ||
        frame_bytes > layout.display[1].size() ||
        inference_bytes > layout.inference[0].size() ||
        inference_bytes > layout.inference[1].size() ||
        scratch_bytes > layout.inference_scratch.size()) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(inference_bytes),
                    "memory.initialize.layout_capacity");
    }

    display_[0].buffer = {layout.display[0].address(),
                          frame_bytes,
                          0U, Region::kDisplay};
    display_[1].buffer = {layout.display[1].address(),
                          frame_bytes,
                          1U, Region::kDisplay};
    inference_[0].buffer = {layout.inference[0].address(),
                             inference_bytes, 0U,
                             Region::kInference};
    inference_[1].buffer = {layout.inference[1].address(),
                             inference_bytes, 1U,
                             Region::kInference};
    current_display_ = -1;
    pending_display_ = -1;
    capture_sequence_ = 0U;
    capture_generation_[0] = 0U;
    capture_generation_[1] = 0U;
    inference_capture_sequence_[0] = 0U;
    inference_capture_sequence_[1] = 0U;
    initialized_ = true;
    return Make(common::ErrorCode::kOk, 0U, "memory.initialize");
}

common::Error MemoryAllocator::CaptureBuffers(std::uintptr_t *first,
                                    std::uintptr_t *second) const
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.capture_buffers.not_initialized");
    }
    if (first == nullptr || second == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.capture_buffers.null_output");
    }
    *first = static_memory_layout::kLayout.capture[0].address();
    *second = static_memory_layout::kLayout.capture[1].address();
    return Make(common::ErrorCode::kOk, 0U, "memory.capture_buffers");
}

common::Error MemoryAllocator::InferenceBuffers(std::uintptr_t *first,
                                      std::uintptr_t *second) const
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference_buffers.not_initialized");
    }
    if (first == nullptr || second == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.inference_buffers.null_output");
    }
    *first = static_memory_layout::kLayout.inference[0].address();
    *second = static_memory_layout::kLayout.inference[1].address();
    return Make(common::ErrorCode::kOk, 0U, "memory.inference_buffers");
}

common::Error MemoryAllocator::ImportCompletedCapture(std::uintptr_t address,
                                            CaptureFrame *frame)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.capture.import.not_initialized");
    }
    if (frame == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.capture.import.null_output");
    }

    std::uint8_t index = 0U;
    if (address == static_memory_layout::kLayout.capture[1].address()) {
        index = 1U;
    } else if (address != static_memory_layout::kLayout.capture[0].address()) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.capture.import.unknown_address");
    }

    ++capture_sequence_;
    capture_generation_[index] = capture_sequence_;
    frame->buffer = {address, kConfig.frame_bytes(), index,
                     Region::kCapture};
    frame->sequence = capture_sequence_;
    return Make(common::ErrorCode::kOk, frame->sequence, "memory.capture.import");
}

common::Error MemoryAllocator::ValidateCaptureFrame(const CaptureFrame &frame) const
{
    if (!initialized_) {
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
            ? static_memory_layout::kLayout.capture[0].address()
            : static_memory_layout::kLayout.capture[1].address();
    if (frame.buffer.address != expected_address ||
        frame.buffer.size != kConfig.frame_bytes()) {
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

common::Error MemoryAllocator::AcquireDisplayBuffer(DisplayBuffer *buffer)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.display.acquire.not_initialized");
    }
    if (buffer == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
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
            return Make(common::ErrorCode::kOk, i, "memory.display.acquire");
        }
    }
    return Make(common::ErrorCode::kNoBuffer, 0U, "memory.display.acquire.no_free_slot");
}

common::Error MemoryAllocator::CommitDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!initialized_) {
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

    Slot &slot = display_[buffer.buffer.index];
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
    slot.state = BufferState::kReady;
    pending_display_ = static_cast<std::int8_t>(buffer.buffer.index);
    return Make(common::ErrorCode::kOk, buffer.buffer.index, "memory.display.commit");
}

common::Error MemoryAllocator::CompleteDisplayHandoff()
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.display.handoff.not_initialized");
    }
    if (pending_display_ < 0) {
        return Make(common::ErrorCode::kOk, 0U, "memory.display.handoff.none");
    }

    const auto pending = static_cast<std::uint8_t>(pending_display_);
    if (display_[pending].state != BufferState::kReady) {
        return Make(common::ErrorCode::kInvalidState,
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
    return Make(common::ErrorCode::kOk, pending, "memory.display.handoff");
}

common::Error MemoryAllocator::ReleaseDisplayBuffer(const DisplayBuffer &buffer)
{
    if (!initialized_) {
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

    Slot &slot = display_[buffer.buffer.index];
    if (!SameBuffer(slot.buffer, buffer.buffer)) {
        return Make(common::ErrorCode::kOwnership, buffer.buffer.index,
                    "memory.display.release.not_owner");
    }
    if (slot.state != BufferState::kFilling) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.display.release.expected_filling");
    }
    slot.state = BufferState::kFree;
    return Make(common::ErrorCode::kOk, buffer.buffer.index, "memory.display.release");
}

common::Error MemoryAllocator::AcquireInferenceBuffer(const CaptureFrame &capture,
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

    for (std::uint8_t i = 0U; i < 2U; ++i) {
        if (inference_[i].state == BufferState::kFree) {
            inference_[i].state = BufferState::kReadyForAi;
            inference_capture_sequence_[i] = capture.sequence;
            PopulateInferenceFrame(i, capture.sequence, false, frame);
            return Make(common::ErrorCode::kOk, i, "memory.inference.acquire");
        }
    }
    return Make(common::ErrorCode::kNoBuffer, 0U,
                "memory.inference.acquire.no_free_slot");
}

common::Error MemoryAllocator::ImportCompletedInference(std::uintptr_t address,
                                               std::uint32_t sequence,
                                               InferenceFrame *frame)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.import.not_initialized");
    }
    if (frame == nullptr || sequence == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, sequence,
                    "memory.inference.import.invalid_argument");
    }

    std::uint8_t index = 0U;
    if (address == static_memory_layout::kLayout.inference[1].address()) {
        index = 1U;
    } else if (address != static_memory_layout::kLayout.inference[0].address()) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.inference.import.unknown_address");
    }

    Slot &slot = inference_[index];
    if (slot.state != BufferState::kFree) {
        return Make(common::ErrorCode::kNoBuffer,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.import.buffer_busy");
    }
    slot.state = BufferState::kReadyForAi;
    inference_capture_sequence_[index] = sequence;
    PopulateInferenceFrame(index, sequence, true, frame);
    return Make(common::ErrorCode::kOk, index, "memory.inference.import");
}

bool MemoryAllocator::IsInferenceBufferFree(std::uintptr_t address) const
{
    if (!initialized_) {
        return false;
    }
    const std::uint8_t index =
        address == static_memory_layout::kLayout.inference[1].address() ? 1U
                                                                         : 0U;
    if (address != static_memory_layout::kLayout.inference[0].address() &&
        address != static_memory_layout::kLayout.inference[1].address()) {
        return false;
    }
    return inference_[index].state == BufferState::kFree;
}

common::Error MemoryAllocator::ClaimInferenceBuffer(const InferenceFrame &frame)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.claim.not_initialized");
    }
    if (frame.buffer.region != Region::kInference) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(frame.buffer.region),
                    "memory.inference.claim.wrong_region");
    }
    if (frame.buffer.index >= 2U) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.claim.bad_index");
    }

    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        inference_capture_sequence_[frame.buffer.index] !=
            frame.capture_sequence) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.claim.not_owner_or_stale");
    }
    if (slot.state != BufferState::kReadyForAi) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.claim.expected_ready");
    }
    slot.state = BufferState::kInUseByAi;
    return Make(common::ErrorCode::kOk, frame.buffer.index,
                "memory.inference.claim");
}

common::Error MemoryAllocator::ReleaseInferenceBuffer(const InferenceFrame &frame)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.release.not_initialized");
    }
    if (frame.buffer.region != Region::kInference) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(frame.buffer.region),
                    "memory.inference.release.wrong_region");
    }
    if (frame.buffer.index >= 2U) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.release.bad_index");
    }

    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        inference_capture_sequence_[frame.buffer.index] !=
            frame.capture_sequence) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.release.not_owner_or_stale");
    }
    if (slot.state != BufferState::kInUseByAi &&
        slot.state != BufferState::kReadyForAi) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot.state),
                    "memory.inference.release.not_in_use");
    }
    slot.state = BufferState::kFree;
    inference_capture_sequence_[frame.buffer.index] = 0U;
    return Make(common::ErrorCode::kOk, frame.buffer.index,
                "memory.inference.release");
}

} // namespace uai::ai::memory_allocator
