#ifndef UAI_AI_MIDDLEWARE_BUFFER_LEASE_POOL_HPP
#define UAI_AI_MIDDLEWARE_BUFFER_LEASE_POOL_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "middleware/buffer/buffer_types.hpp"

namespace uai::ai::buffer {

/*
 * Fixed set of buffers handed out under a lease token. The pool does not
 * know why a buffer is held: the owner chooses the state values and
 * serializes access. The pool only guarantees that a token identifies one
 * acquisition of one slot, so a stale token never matches a re-leased slot.
 */
template <std::size_t Capacity>
class LeasePool final {
    static_assert(Capacity > 0U && Capacity <= 0xFFU);

public:
    struct Slot {
        Buffer buffer{};
        BufferState state = BufferState::kFree;
        std::uint64_t lease_token = 0U;
    };

    static constexpr std::size_t capacity() { return Capacity; }

    Slot &operator[](std::uint8_t index) { return slots_[index]; }
    const Slot &operator[](std::uint8_t index) const { return slots_[index]; }

    Slot *Find(std::uint8_t index)
    {
        return index < Capacity ? &slots_[index] : nullptr;
    }

    const Slot *Find(std::uint8_t index) const
    {
        return index < Capacity ? &slots_[index] : nullptr;
    }

    Slot *FindByAddress(std::uintptr_t address)
    {
        for (Slot &slot : slots_) {
            if (slot.buffer.address == address) return &slot;
        }
        return nullptr;
    }

    const Slot *FindByAddress(std::uintptr_t address) const
    {
        for (const Slot &slot : slots_) {
            if (slot.buffer.address == address) return &slot;
        }
        return nullptr;
    }

    Slot *FindFree()
    {
        for (Slot &slot : slots_) {
            if (slot.state == BufferState::kFree) return &slot;
        }
        return nullptr;
    }

    /* Enter `next` and mint a token that is never zero. */
    std::uint64_t Lease(Slot &slot, BufferState next)
    {
        ++next_token_;
        if (next_token_ == 0U) ++next_token_;
        slot.state = next;
        slot.lease_token = next_token_;
        return slot.lease_token;
    }

    void Release(Slot &slot)
    {
        slot.state = BufferState::kFree;
        slot.lease_token = 0U;
    }

private:
    std::array<Slot, Capacity> slots_{};
    std::uint64_t next_token_ = 0U;
};

} // namespace uai::ai::buffer

#endif
