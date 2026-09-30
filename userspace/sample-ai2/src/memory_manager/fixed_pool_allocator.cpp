#include "memory_manager/fixed_pool_allocator.hpp"

#include "memory_manager/memory_sizes.hpp"
#include "memory_manager/static_memory_layout.hpp"

#if defined(__arm__) || defined(__thumb__)
extern "C" {
#include "stm32n6xx_hal.h"
}
#endif

namespace uai::ai::memory_allocator {


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

} // namespace

namespace {

inline constexpr BufferPoolId kDisplayPool = 1U;
inline constexpr BufferPoolId kInferencePool = 2U;

class StateLock final {
public:
    StateLock()
    {
#if defined(__arm__) || defined(__thumb__)
        primask_ = __get_PRIMASK();
        __disable_irq();
#endif
    }

    ~StateLock()
    {
#if defined(__arm__) || defined(__thumb__)
        __set_PRIMASK(primask_);
#endif
    }

    StateLock(const StateLock &) = delete;
    StateLock &operator=(const StateLock &) = delete;

private:
#if defined(__arm__) || defined(__thumb__)
    std::uint32_t primask_ = 0U;
#endif
};

} // namespace

common::Error FixedPoolAllocator::Make(common::ErrorCode code, std::uint32_t detail,
                          const char *operation)
{
    return {code, detail, operation};
}

bool FixedPoolAllocator::SameBuffer(const Buffer &lhs, const Buffer &rhs)
{
    return lhs.address == rhs.address && lhs.size == rhs.size &&
           lhs.index == rhs.index && lhs.region == rhs.region;
}

void FixedPoolAllocator::ReleasePointer(
    void *context, const memory_manager::PointerIdentity &identity) noexcept
{
    if (context != nullptr) {
        static_cast<FixedPoolAllocator *>(context)->ReleasePointer(identity);
    }
}

common::Error FixedPoolAllocator::ValidatePointer(
    const void *context,
    const memory_manager::PointerIdentity &identity) noexcept
{
    if (context == nullptr) {
        return Make(common::ErrorCode::kOwnership, 0U,
                    "memory.pointer.validate.no_allocator");
    }
    return static_cast<const FixedPoolAllocator *>(context)
        ->Validate(identity);
}

std::uint64_t FixedPoolAllocator::NextInferenceLeaseToken()
{
    ++next_inference_lease_token_;
    if (next_inference_lease_token_ == 0U) {
        ++next_inference_lease_token_;
    }
    return next_inference_lease_token_;
}

std::uint64_t FixedPoolAllocator::NextLeaseToken()
{
    ++next_lease_token_;
    if (next_lease_token_ == 0U) {
        ++next_lease_token_;
    }
    return next_lease_token_;
}

FixedPoolAllocator::Slot *FixedPoolAllocator::FindSlot(
    BufferPoolId pool, std::uint8_t index)
{
    if (pool == kDisplayPool) {
        return index < display_.size() ? &display_[index] : nullptr;
    }
    if (pool == kInferencePool) {
        return index < inference_.size() ? &inference_[index] : nullptr;
    }
    return nullptr;
}

const FixedPoolAllocator::Slot *FixedPoolAllocator::FindSlot(
    BufferPoolId pool, std::uint8_t index) const
{
    if (pool == kDisplayPool) {
        return index < display_.size() ? &display_[index] : nullptr;
    }
    if (pool == kInferencePool) {
        return index < inference_.size() ? &inference_[index] : nullptr;
    }
    return nullptr;
}

std::size_t FixedPoolAllocator::PoolSize(BufferPoolId pool) const
{
    if (pool == kDisplayPool) {
        return display_.size();
    }
    if (pool == kInferencePool) {
        return inference_.size();
    }
    return 0U;
}

common::Error FixedPoolAllocator::Inspect(BufferPoolId pool,
                                          std::uint8_t index,
                                          SlotInfo *slot) const
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.pool.inspect.not_initialized");
    }
    if (slot == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.pool.inspect.null_output");
    }

    const Slot *entry = FindSlot(pool, index);
    if (entry == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, index,
                    "memory.pool.inspect.invalid_slot");
    }

    slot->buffer = entry->buffer;
    slot->state = entry->state;
    slot->lease_token = pool == kInferencePool
                            ? inference_lease_token_[index]
                            : display_lease_token_[index];
    return Make(common::ErrorCode::kOk, index, "memory.pool.inspect");
}

common::Error FixedPoolAllocator::Transition(BufferPoolId pool,
                                             std::uint8_t index,
                                             BufferState expected,
                                             BufferState next)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.pool.transition.not_initialized");
    }

    StateLock lock;
    Slot *slot = FindSlot(pool, index);
    if (slot == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, index,
                    "memory.pool.transition.invalid_slot");
    }
    if (slot->state != expected) {
        return Make(common::ErrorCode::kInvalidState,
                    static_cast<std::uint32_t>(slot->state),
                    "memory.pool.transition.unexpected_state");
    }
    slot->state = next;
    if (next == BufferState::kFree) {
        if (pool == kInferencePool) {
            inference_lease_token_[index] = 0U;
        } else if (pool == kDisplayPool) {
            display_lease_token_[index] = 0U;
        }
    }
    return Make(common::ErrorCode::kOk, index, "memory.pool.transition");
}

common::Error FixedPoolAllocator::ReserveSlot(
    BufferPoolId pool, std::uint8_t index, BufferState expected,
    BufferState next, std::uint64_t *lease_token)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.pool.reserve.not_initialized");
    }
    if (lease_token == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.pool.reserve.null_output");
    }
    if (next == BufferState::kFree) {
        return Make(common::ErrorCode::kInvalidArgument, index,
                    "memory.pool.reserve.free_target");
    }

    StateLock lock;
    Slot *slot = FindSlot(pool, index);
    if (slot == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, index,
                    "memory.pool.reserve.invalid_slot");
    }
    if (slot->state != expected) {
        return Make(common::ErrorCode::kNoBuffer, index,
                    "memory.pool.reserve.unexpected_state");
    }

    slot->state = next;
    if (pool == kInferencePool) {
        inference_lease_token_[index] = NextInferenceLeaseToken();
        *lease_token = inference_lease_token_[index];
    } else {
        display_lease_token_[index] = NextLeaseToken();
        *lease_token = display_lease_token_[index];
    }
    return Make(common::ErrorCode::kOk, index,
                "memory.pool.reserve");
}

common::Error FixedPoolAllocator::Acquire(
    const BufferRequest &request, memory_manager::UniquePointer *pointer)
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.pointer.acquire.not_initialized");
    }
    if (pointer == nullptr) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.pointer.acquire.null_output");
    }
    *pointer = {};

    memory_manager::PointerIdentity identity{};
    memory_manager::PointerControl *control = nullptr;

    if (request.pool == kDisplayPool) {
        Buffer buffer{};
        common::Error status = {};
        if (request.index == kAnyBufferIndex) {
            for (std::uint8_t candidate = 0U;
                 candidate < display_.size(); ++candidate) {
                if (display_[candidate].state == BufferState::kFree) {
                    display_[candidate].state = BufferState::kFilling;
                    buffer = display_[candidate].buffer;
                    status = Make(common::ErrorCode::kOk, candidate,
                                  "memory.pointer.acquire.display");
                    break;
                }
            }
            if (!status.Ok()) {
                status = Make(common::ErrorCode::kNoBuffer, 0U,
                              "memory.pointer.acquire.display.no_free_slot");
            }
        } else {
            if (request.index >= display_.size()) {
                return Make(common::ErrorCode::kInvalidArgument, request.index,
                            "memory.pointer.acquire.display.bad_index");
            }
            StateLock lock;
            Slot &slot = display_[request.index];
            if (slot.state != BufferState::kFree) {
                return Make(common::ErrorCode::kNoBuffer, request.index,
                            "memory.pointer.acquire.display.busy");
            }
            slot.state = BufferState::kFilling;
            buffer = slot.buffer;
            status = Make(common::ErrorCode::kOk, request.index,
                           "memory.pointer.acquire.display");
        }
        if (!status.Ok()) {
            return status;
        }

        const std::uint8_t index = buffer.index;
        if ((request.size != 0U && request.size > buffer.size) ||
            (request.alignment != 0U &&
             (buffer.address % request.alignment) != 0U)) {
            display_[index].state = BufferState::kFree;
            return Make(common::ErrorCode::kInvalidArgument, index,
                        "memory.pointer.acquire.display.requirements");
        }
        display_lease_token_[index] = NextLeaseToken();
        identity = {buffer, kDisplayPool, display_lease_token_[index]};
        control = &display_[index].pointer;
    } else if (request.pool == kInferencePool) {
        std::uint8_t index = request.index;
        if (index == kAnyBufferIndex) {
            for (std::uint8_t candidate = 0U;
                 candidate < inference_.size(); ++candidate) {
                if (inference_[candidate].state == BufferState::kFree) {
                    index = candidate;
                    break;
                }
            }
        }
        if (index >= inference_.size()) {
            return Make(common::ErrorCode::kNoBuffer, request.index,
                        "memory.pointer.acquire.inference.no_free_slot");
        }

        StateLock lock;
        Slot &slot = inference_[index];
        if (slot.state != BufferState::kFree) {
            return Make(common::ErrorCode::kNoBuffer, index,
                        "memory.pointer.acquire.inference.busy");
        }
        if ((request.size != 0U && request.size > slot.buffer.size) ||
            (request.alignment != 0U &&
             (slot.buffer.address % request.alignment) != 0U)) {
            return Make(common::ErrorCode::kInvalidArgument, index,
                        "memory.pointer.acquire.inference.requirements");
        }
        slot.state = BufferState::kReadyForAi;
        inference_lease_token_[index] = NextInferenceLeaseToken();
        identity = {slot.buffer, kInferencePool, inference_lease_token_[index]};
        control = &inference_[index].pointer;
    } else {
        return Make(common::ErrorCode::kInvalidArgument, request.pool,
                    "memory.pointer.acquire.unknown_pool");
    }

    control->Initialize(identity, this, &FixedPoolAllocator::ReleasePointer,
                        &FixedPoolAllocator::ValidatePointer);
    *pointer = memory_manager::UniquePointer(control);
    return Make(common::ErrorCode::kOk,
                static_cast<std::uint32_t>(identity.buffer.index),
                "memory.pointer.acquire");
}

common::Error FixedPoolAllocator::Validate(
    const memory_manager::PointerIdentity &identity) const
{
    if (!initialized_) {
        return Make(common::ErrorCode::kNotInitialized, 0U,
                    "memory.pointer.validate.not_initialized");
    }
    if (!identity.buffer || identity.token == 0U) {
        return Make(common::ErrorCode::kInvalidArgument, 0U,
                    "memory.pointer.validate.empty");
    }

    if (identity.pool == kDisplayPool) {
        if (identity.buffer.index >= display_.size()) {
            return Make(common::ErrorCode::kInvalidArgument, identity.buffer.index,
                        "memory.pointer.validate.display.bad_index");
        }
        const Slot &slot = display_[identity.buffer.index];
        if (!SameBuffer(slot.buffer, identity.buffer) ||
            display_lease_token_[identity.buffer.index] != identity.token) {
            return Make(common::ErrorCode::kOwnership, identity.buffer.index,
                        "memory.pointer.validate.display.not_owner");
        }
        if (slot.state != BufferState::kFilling) {
            return Make(common::ErrorCode::kInvalidState,
                        static_cast<std::uint32_t>(slot.state),
                        "memory.pointer.validate.display.not_filling");
        }
        return Make(common::ErrorCode::kOk, identity.buffer.index,
                    "memory.pointer.validate.display");
    }

    if (identity.pool == kInferencePool) {
        if (identity.buffer.index >= inference_.size()) {
            return Make(common::ErrorCode::kInvalidArgument, identity.buffer.index,
                        "memory.pointer.validate.inference.bad_index");
        }
        const Slot &slot = inference_[identity.buffer.index];
        if (!SameBuffer(slot.buffer, identity.buffer) ||
            inference_lease_token_[identity.buffer.index] != identity.token) {
            return Make(common::ErrorCode::kOwnership, identity.buffer.index,
                        "memory.pointer.validate.inference.not_owner");
        }
        if (slot.state != BufferState::kReadyForAi &&
            slot.state != BufferState::kInUseByAi) {
            return Make(common::ErrorCode::kInvalidState,
                        static_cast<std::uint32_t>(slot.state),
                        "memory.pointer.validate.inference.not_owned");
        }
        return Make(common::ErrorCode::kOk, identity.buffer.index,
                    "memory.pointer.validate.inference");
    }

    return Make(common::ErrorCode::kInvalidArgument, identity.pool,
                "memory.pointer.validate.unknown_pool");
}

void FixedPoolAllocator::ReleasePointer(
    const memory_manager::PointerIdentity &identity) noexcept
{
    StateLock lock;
    if (identity.pool == kDisplayPool &&
        identity.buffer.index < display_.size()) {
        Slot &slot = display_[identity.buffer.index];
        if (SameBuffer(slot.buffer, identity.buffer) &&
            display_lease_token_[identity.buffer.index] == identity.token &&
            slot.state == BufferState::kFilling) {
            slot.state = BufferState::kFree;
            display_lease_token_[identity.buffer.index] = 0U;
        }
        return;
    }

    if (identity.pool == kInferencePool &&
        identity.buffer.index < inference_.size()) {
        Slot &slot = inference_[identity.buffer.index];
        if (SameBuffer(slot.buffer, identity.buffer) &&
            inference_lease_token_[identity.buffer.index] == identity.token &&
            (slot.state == BufferState::kReadyForAi ||
             slot.state == BufferState::kInUseByAi)) {
            slot.state = BufferState::kFree;
            inference_lease_token_[identity.buffer.index] = 0U;
        }
    }
}

common::Error FixedPoolAllocator::Initialize()
{
    if (initialized_) {
        return Make(common::ErrorCode::kAlreadyInitialized, 0U, "memory.initialize");
    }

    constexpr std::size_t region_count =
        static_cast<std::size_t>(StaticMemoryKey::kCount);
    static_memory_layout::AddressRange ranges[region_count]{};
    for (std::size_t i = 0U; i < region_count; ++i) {
        const auto &region = static_memory_layout::GetRegionByIndex(i);
        if (!static_memory_layout::IsValidRegion(region)) {
            return Make(common::ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(i),
                        "memory.initialize.invalid_region");
        }
        ranges[i] = static_memory_layout::GetAddressRange(region);
        if ((region.address() % memory_manager::kMemoryConfig.buffer_alignment) != 0U) {
            return Make(common::ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(i),
                        "memory.initialize.region_alignment");
        }
    }
    for (std::size_t lhs = 0U; lhs < region_count; ++lhs) {
        for (std::size_t rhs = lhs + 1U; rhs < region_count; ++rhs) {
            if (static_memory_layout::RegionsOverlap(ranges[lhs], ranges[rhs])) {
                return Make(common::ErrorCode::kInvalidArgument,
                            static_cast<std::uint32_t>(lhs),
                            "memory.initialize.overlapping_regions");
            }
        }
    }

    const std::size_t frame_bytes = memory_manager::kCaptureBufferBytes;
    const std::size_t inference_bytes = memory_manager::kInferenceBufferBytes;
    const std::size_t source_bytes = memory_manager::kInferenceSourceBytes;
    const std::size_t scratch_bytes = memory_manager::kInferenceScratchBytes;
    if (frame_bytes > StaticRegion(StaticMemoryKey::kCapture0).size() ||
        frame_bytes > StaticRegion(StaticMemoryKey::kCapture1).size() ||
        frame_bytes > StaticRegion(StaticMemoryKey::kDisplay0).size() ||
        frame_bytes > StaticRegion(StaticMemoryKey::kDisplay1).size() ||
        source_bytes >
            StaticRegion(StaticMemoryKey::kInferenceSource0).size() ||
        source_bytes >
            StaticRegion(StaticMemoryKey::kInferenceSource1).size() ||
        source_bytes >
            StaticRegion(StaticMemoryKey::kInferenceSource2).size() ||
        scratch_bytes >
            StaticRegion(StaticMemoryKey::kInferenceScratch).size()) {
        return Make(common::ErrorCode::kInvalidArgument,
                    static_cast<std::uint32_t>(inference_bytes),
                    "memory.initialize.layout_capacity");
    }

    display_[0].buffer = {StaticRegion(StaticMemoryKey::kDisplay0).address(),
                          frame_bytes,
                          0U, Region::kDisplay};
    display_[1].buffer = {StaticRegion(StaticMemoryKey::kDisplay1).address(),
                          frame_bytes,
                          1U, Region::kDisplay};
    for (std::size_t i = 0U; i < memory_manager::kInferenceBufferCount; ++i) {
        if (inference_bytes > InferenceRegion(i).size()) {
            return Make(common::ErrorCode::kInvalidArgument,
                        static_cast<std::uint32_t>(inference_bytes),
                        "memory.initialize.layout_capacity");
        }
        inference_[i].buffer = {InferenceRegion(i).address(), inference_bytes,
                                 static_cast<std::uint8_t>(i), Region::kInference};
    }
    initialized_ = true;
    return Make(common::ErrorCode::kOk, 0U, "memory.initialize");
}

} // namespace uai::ai::memory_allocator
