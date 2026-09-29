#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::buffer_layout {

/* Hardware-independent ownership state machine. Caller serializes operations
 * across ISR/tasks. A 64-bit lease token prevents delayed queue messages from
 * claiming a recycled slot even when 32-bit capture sequence wraps. */
template <std::size_t Slots>
class LeasePool final {
public:
    enum class State : std::uint8_t { kFree, kReady, kOwned };

    bool Free(std::size_t index) const
    {
        return index < Slots && slots_[index].state == State::kFree;
    }
    State StateAt(std::size_t index) const { return slots_[index].state; }
    std::uint32_t SequenceAt(std::size_t index) const
    {
        return slots_[index].sequence;
    }
    std::uint64_t TokenAt(std::size_t index) const
    {
        return slots_[index].token;
    }

    bool Reserve(std::size_t index, std::uint32_t sequence)
    {
        if (!Free(index) || sequence == 0U) {
            return false;
        }
        Slot &slot = slots_[index];
        slot.token = ++next_token_;
        if (slot.token == 0U) {
            slot.token = ++next_token_;
        }
        slot.sequence = sequence;
        slot.state = State::kReady;
        return true;
    }
    bool Matches(std::size_t index, std::uint32_t sequence,
                 std::uint64_t token) const
    {
        return index < Slots && token != 0U &&
               slots_[index].state != State::kFree &&
               slots_[index].sequence == sequence &&
               slots_[index].token == token;
    }
    bool Claim(std::size_t index, std::uint32_t sequence,
               std::uint64_t token)
    {
        if (!Matches(index, sequence, token) ||
            slots_[index].state != State::kReady) {
            return false;
        }
        slots_[index].state = State::kOwned;
        return true;
    }
    bool Release(std::size_t index, std::uint32_t sequence,
                 std::uint64_t token)
    {
        if (!Matches(index, sequence, token)) {
            return false;
        }
        slots_[index].sequence = 0U;
        slots_[index].state = State::kFree;
        return true;
    }
    /* Camera ISR has only an address/sequence, not a frame token. Dropping
     * is allowed solely for a still-unclaimed completion. */
    bool DropReady(std::size_t index, std::uint32_t sequence)
    {
        if (index >= Slots || slots_[index].state != State::kReady ||
            slots_[index].sequence != sequence || sequence == 0U) {
            return false;
        }
        slots_[index].sequence = 0U;
        slots_[index].state = State::kFree;
        return true;
    }

private:
    struct Slot {
        State state = State::kFree;
        std::uint32_t sequence = 0U;
        std::uint64_t token = 0U;
    };
    Slot slots_[Slots]{};
    std::uint64_t next_token_ = 0U;
};

} // namespace uai::ai::buffer_layout
