#include "memory_allocator/memory_allocator.hpp"

#include "buffer_layout/region_guard.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "sample_ai_config.hpp"
#include "static_memory_layout/static_memory_layout.hpp"

#if defined(__arm__)
#include "stm32n6xx_hal.h"
#endif

namespace uai::ai::memory_allocator {

namespace {

using StaticMemoryKey = static_memory_layout::Key;

/* ISR and task share inference slot state. Mask interrupts only for the tiny
 * state transition; never while reading PSRAM or maintaining cache lines. */
class StateLock final {
public:
    StateLock()
    {
#if defined(__arm__)
        primask_ = __get_PRIMASK();
        __disable_irq();
#endif
    }
    ~StateLock()
    {
#if defined(__arm__)
        __set_PRIMASK(primask_);
#endif
    }
    StateLock(const StateLock &) = delete;
    StateLock &operator=(const StateLock &) = delete;

private:
#if defined(__arm__)
    std::uint32_t primask_ = 0U;
#endif
};

constexpr StaticMemoryKey kInferenceKeys[] = {
    StaticMemoryKey::kInference0, StaticMemoryKey::kInference1,
    StaticMemoryKey::kInference2, StaticMemoryKey::kInference3,
    StaticMemoryKey::kInference4};
constexpr StaticMemoryKey kSourceKeys[] = {
    StaticMemoryKey::kInferenceSource0, StaticMemoryKey::kInferenceSource1,
    StaticMemoryKey::kInferenceSource2, StaticMemoryKey::kInferenceSource3,
    StaticMemoryKey::kInferenceSource4};
static_assert(sizeof(kInferenceKeys) / sizeof(kInferenceKeys[0]) ==
                  kInferenceBufferCount);
static_assert(sizeof(kSourceKeys) / sizeof(kSourceKeys[0]) ==
                  kInferenceBufferCount);
static_assert(!config::kCamera.raw_dump,
              "The raw dump region is allocated to inference slots");

const static_memory_layout::Region &StaticRegion(StaticMemoryKey key)
{
    return static_memory_layout::kLayout.Get(key);
}

const static_memory_layout::Region &InferenceRegion(std::size_t index)
{
    return StaticRegion(kInferenceKeys[index]);
}

const static_memory_layout::Region &InferenceSourceRegion(std::size_t index)
{
    return StaticRegion(kSourceKeys[index]);
}

} // namespace

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
    frame->source = {
        InferenceSourceRegion(index).address(),
        kConfig.inference_source_bytes(),
        index,
        Region::kInference,
        kConfig.buffer_alignment};
    frame->scratch = {
        StaticRegion(StaticMemoryKey::kInferenceScratch).address(),
        kConfig.inference_scratch_bytes(),
        index,
        Region::kInference,
        kConfig.buffer_alignment};
    frame->output_count = static_cast<std::uint8_t>(
        kConfig.model_output_bytes.size());
    frame->capture_sequence = sequence;
    frame->lease_token = inference_leases_.TokenAt(index);
    frame->from_pipe2 = from_pipe2;
    frame->source_valid = false;
    frame->input_prepared_by_cpu = false;
    frame->input_prepared = false;
    frame->prepared_model_kind_id = InferenceFrame::kUnknownModelKindId;
    frame->input_preparation_start_ms = 0U;
    frame->input_preparation_end_ms = 0U;
    frame->input_preparation_elapsed_ms = 0U;

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
    const std::size_t source_bytes = kConfig.inference_source_bytes();
    const std::size_t scratch_bytes = kConfig.inference_scratch_bytes();
    /* Each physical region has a distinct owner. Verify all reservations,
     * not just the five inference ranges (including DMA drop and masks). */
    buffer_layout::Range reservations[static_cast<std::size_t>(
        StaticMemoryKey::kCount)]{};
    for (std::size_t i = 0U; i < sizeof(reservations) / sizeof(reservations[0]);
         ++i) {
        const auto key = static_cast<StaticMemoryKey>(i);
        const auto &region = layout.Get(key);
        reservations[i] = {region.address(), region.size()};
    }
    /* ThreadMonitor lives in AXISRAM, not in PSRAM; still check its range. */
    if (!buffer_layout::Disjoint(reservations,
                                 sizeof(reservations) / sizeof(reservations[0]))) {
        return Make(common::ErrorCode::kOwnership, 0U,
                    "memory.initialize.overlapping_regions");
    }
    if (frame_bytes > layout.Get(StaticMemoryKey::kCapture0).size() ||
        frame_bytes > layout.Get(StaticMemoryKey::kCapture1).size() ||
        frame_bytes > layout.Get(StaticMemoryKey::kDisplay0).size() ||
        frame_bytes > layout.Get(StaticMemoryKey::kDisplay1).size() ||
        source_bytes >
            layout.Get(StaticMemoryKey::kInferenceSource0).size() ||
        source_bytes >
            layout.Get(StaticMemoryKey::kInferenceSource1).size() ||
        source_bytes >
            layout.Get(StaticMemoryKey::kInferenceSource2).size() ||
        scratch_bytes >
            layout.Get(StaticMemoryKey::kInferenceScratch).size()) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(inference_bytes),
                    "memory.initialize.layout_capacity");
    }

    display_[0].buffer = {layout.Get(StaticMemoryKey::kDisplay0).address(),
                          frame_bytes,
                          0U, Region::kDisplay};
    display_[1].buffer = {layout.Get(StaticMemoryKey::kDisplay1).address(),
                          frame_bytes,
                          1U, Region::kDisplay};
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (inference_bytes + buffer_layout::kGuardBytes >
                InferenceRegion(i).size() ||
            source_bytes + buffer_layout::kGuardBytes >
                InferenceSourceRegion(i).size()) {
            return Make(common::ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(inference_bytes),
                        "memory.initialize.layout_capacity");
        }
        inference_[i].buffer = {InferenceRegion(i).address(), inference_bytes,
                                 static_cast<std::uint8_t>(i), Region::kInference};
    }
    current_display_ = -1;
    pending_display_ = -1;
    capture_sequence_ = 0U;
    capture_generation_[0] = 0U;
    capture_generation_[1] = 0U;
    initialized_ = true;
    return Make(common::ErrorCode::kOk, 0U, "memory.initialize");
}

common::Error MemoryAllocator::ArmInferenceGuards(cache::CacheDriver &cache)
{
    if (!initialized_ || guard_cache_ != nullptr) {
        return Make(common::ErrorCode::kInvalidState, 0U, "memory.guards.arm");
    }
    for (std::size_t i = 0U; i < kInferenceBufferCount; ++i) {
        const std::uintptr_t guards[] = {
            inference_[i].buffer.address + inference_[i].buffer.size,
            InferenceSourceRegion(i).address() + kConfig.inference_source_bytes()};
        for (const std::uintptr_t address : guards) {
            buffer_layout::ArmGuard(address);
            const Buffer guard{address, buffer_layout::kGuardBytes,
                               static_cast<std::uint8_t>(i), Region::kInference};
            const common::Error status = cache.PrepareForPeripheralRead(guard);
            if (!status.Ok()) {
                return status;
            }
        }
    }
    guard_cache_ = &cache;
    return Make(common::ErrorCode::kOk, 0U, "memory.guards.arm");
}

common::Error MemoryAllocator::CheckInferenceGuards(
    const InferenceFrame &frame) const
{
    if (guard_cache_ == nullptr || frame.buffer.index >= kInferenceBufferCount) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.guards.unavailable");
    }
    const std::uint8_t index = frame.buffer.index;
    const std::uintptr_t guards[] = {
        inference_[index].buffer.address + inference_[index].buffer.size,
        InferenceSourceRegion(index).address() + kConfig.inference_source_bytes()};
    for (const std::uintptr_t address : guards) {
        const Buffer guard{address, buffer_layout::kGuardBytes, index,
                           Region::kInference};
        const common::Error status = guard_cache_->PrepareForCpuRead(guard);
        if (!status.Ok()) {
            return status;
        }
        if (!buffer_layout::GuardIntact(address)) {
            return Make(common::ErrorCode::kOwnership, index,
                        "memory.guards.overrun");
        }
    }
    return Make(common::ErrorCode::kOk, index, "memory.guards.intact");
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
    *first = StaticRegion(StaticMemoryKey::kCapture0).address();
    *second = StaticRegion(StaticMemoryKey::kCapture1).address();
    return Make(common::ErrorCode::kOk, 0U, "memory.capture_buffers");
}

common::Error MemoryAllocator::InferenceBuffers(std::uintptr_t *buffers,
                                                std::size_t count) const
{
    if (!initialized_) {
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
    if (address == StaticRegion(StaticMemoryKey::kCapture1).address()) {
        index = 1U;
    } else if (address != StaticRegion(StaticMemoryKey::kCapture0).address()) {
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
            ? StaticRegion(StaticMemoryKey::kCapture0).address()
            : StaticRegion(StaticMemoryKey::kCapture1).address();
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

    const StateLock lock;
    for (std::uint8_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (inference_leases_.Reserve(i, capture.sequence)) {
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
    bool found = false;
    for (std::uint8_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (address == InferenceRegion(i).address()) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(address),
                    "memory.inference.import.unknown_address");
    }

    if (inference_leases_.StateAt(index) !=
            buffer_layout::LeasePool<kInferenceBufferCount>::State::kReady ||
        inference_leases_.SequenceAt(index) != sequence) {
        return Make(common::ErrorCode::kOwnership,
                    static_cast<std::uint32_t>(inference_leases_.StateAt(index)),
                    "memory.inference.import.not_reserved");
    }
    PopulateInferenceFrame(index, sequence, true, frame);
    const common::Error guard = CheckInferenceGuards(*frame);
    return guard.Ok() ? Make(common::ErrorCode::kOk, index,
                             "memory.inference.import") : guard;
}

common::Error MemoryAllocator::ReserveCompletedInference(
    std::uintptr_t address, std::uint32_t sequence)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.reserve.not_initialized");
    }
    if (address == 0U || sequence == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, sequence,
                    "memory.inference.reserve.invalid_argument");
    }

    const StateLock lock;
    for (std::uint8_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (address != InferenceRegion(i).address()) {
            continue;
        }
        if (!inference_leases_.Reserve(i, sequence)) {
            return Make(common::ErrorCode::kNoBuffer,
                        static_cast<std::uint32_t>(inference_leases_.StateAt(i)),
                        "memory.inference.reserve.buffer_busy");
        }
        return Make(common::ErrorCode::kOk, i,
                    "memory.inference.reserve");
    }
    return Make(common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(address),
                "memory.inference.reserve.unknown_address");
}

common::Error MemoryAllocator::DropCompletedInference(
    std::uintptr_t address, std::uint32_t sequence)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.inference.drop.not_initialized");
    }
    if (address == 0U || sequence == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, sequence,
                    "memory.inference.drop.invalid_argument");
    }

    const StateLock lock;
    for (std::uint8_t i = 0U; i < kInferenceBufferCount; ++i) {
        if (address != InferenceRegion(i).address()) {
            continue;
        }
        if (!inference_leases_.DropReady(i, sequence)) {
            return Make(common::ErrorCode::kOwnership,
                        static_cast<std::uint32_t>(inference_leases_.StateAt(i)),
                        "memory.inference.drop.not_reserved");
        }
        return Make(common::ErrorCode::kOk, i,
                    "memory.inference.drop");
    }
    return Make(common::ErrorCode::kInvalidArgument,
                static_cast<std::uint32_t>(address),
                "memory.inference.drop.unknown_address");
}

bool MemoryAllocator::IsInferenceBufferFree(std::uintptr_t address) const
{
    if (!initialized_) {
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
    const StateLock lock;
    return inference_leases_.Free(index);
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
    if (frame.buffer.index >= kInferenceBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.claim.bad_index");
    }

    Slot &slot = inference_[frame.buffer.index];
    const auto &expected_source = InferenceSourceRegion(frame.buffer.index);
    const auto &expected_scratch = StaticRegion(StaticMemoryKey::kInferenceScratch);
    if (frame.source.address != expected_source.address() ||
        frame.source.size != kConfig.inference_source_bytes() ||
        frame.scratch.address != expected_scratch.address() ||
        frame.scratch.size != kConfig.inference_scratch_bytes() ||
        frame.output_count != kConfig.model_output_bytes.size()) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.claim.invalid_regions");
    }
    std::uintptr_t output_address =
        frame.buffer.address + kConfig.inference_outputs_offset();
    for (std::size_t i = 0U; i < frame.output_count; ++i) {
        if (frame.outputs[i].address != output_address ||
            frame.outputs[i].size != kConfig.model_output_bytes[i] ||
            !buffer_layout::Contains(
                {frame.buffer.address, frame.buffer.size},
                {frame.outputs[i].address, frame.outputs[i].size})) {
            return Make(common::ErrorCode::kOwnership,
                        static_cast<std::uint32_t>(i),
                        "memory.inference.claim.output_bounds");
        }
        output_address += kConfig.AlignUp(kConfig.model_output_bytes[i]);
    }
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        !inference_leases_.Matches(frame.buffer.index, frame.capture_sequence,
                                   frame.lease_token)) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.claim.not_owner_or_stale");
    }
    if (inference_leases_.StateAt(frame.buffer.index) !=
        buffer_layout::LeasePool<kInferenceBufferCount>::State::kReady) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(inference_leases_.StateAt(
                        frame.buffer.index)),
                    "memory.inference.claim.expected_ready");
    }
    const common::Error guard = CheckInferenceGuards(frame);
    if (!guard.Ok()) {
        return guard;
    }
    const StateLock lock;
    if (!inference_leases_.Claim(frame.buffer.index,
                                  frame.capture_sequence, frame.lease_token)) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.claim.changed");
    }
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
    if (frame.buffer.index >= kInferenceBufferCount) {
        return Make(common::ErrorCode::kInvalidArgument, frame.buffer.index,
                    "memory.inference.release.bad_index");
    }

    Slot &slot = inference_[frame.buffer.index];
    if (!SameBuffer(slot.buffer, frame.buffer) ||
        !inference_leases_.Matches(frame.buffer.index, frame.capture_sequence,
                                   frame.lease_token)) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.release.not_owner_or_stale");
    }
    if (inference_leases_.Free(frame.buffer.index)) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(inference_leases_.StateAt(
                        frame.buffer.index)),
                    "memory.inference.release.not_in_use");
    }
    const common::Error guard = CheckInferenceGuards(frame);
    if (!guard.Ok()) {
        return guard;
    }
    const StateLock lock;
    if (!inference_leases_.Release(frame.buffer.index, frame.capture_sequence,
                                    frame.lease_token)) {
        return Make(common::ErrorCode::kOwnership, frame.buffer.index,
                    "memory.inference.release.changed");
    }
    return Make(common::ErrorCode::kOk, frame.buffer.index,
                "memory.inference.release");
}

} // namespace uai::ai::memory_allocator
