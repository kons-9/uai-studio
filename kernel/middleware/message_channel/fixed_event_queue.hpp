#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace uai::ai::message_channel {

/* Fixed-capacity ring holding Capacity - 1 events for a single consumer.
 * Producers must be serialized by the caller; a full ring counts the drop
 * instead of blocking, and Drain() reads at most Capacity events. */
template <typename Event, std::size_t Capacity>
class FixedEventQueue final {
    static_assert(std::is_trivially_copyable_v<Event>);
    static_assert(Capacity > 1U);

public:
    void Reset()
    {
        __atomic_store_n(&write_index_, 0U, __ATOMIC_RELEASE);
        __atomic_store_n(&read_index_, 0U, __ATOMIC_RELEASE);
        __atomic_store_n(&dropped_count_, 0U, __ATOMIC_RELEASE);
    }

    bool Push(const Event &event)
    {
        const std::uint32_t write = __atomic_load_n(&write_index_, __ATOMIC_RELAXED);
        const std::uint32_t read = __atomic_load_n(&read_index_, __ATOMIC_ACQUIRE);
        const std::uint32_t next = (write + 1U) % static_cast<std::uint32_t>(Capacity);
        if (next == read) {
            __atomic_fetch_add(&dropped_count_, 1U, __ATOMIC_RELAXED);
            return false;
        }
        events_[write] = event;
        __atomic_store_n(&write_index_, next, __ATOMIC_RELEASE);
        return true;
    }

    template <typename Consume>
    std::size_t Drain(Consume consume)
    {
        std::uint32_t read = __atomic_load_n(&read_index_, __ATOMIC_RELAXED);
        std::size_t count = 0U;
        while (count < Capacity) {
            const std::uint32_t write = __atomic_load_n(&write_index_, __ATOMIC_ACQUIRE);
            if (read == write)
                break;
            const Event event = events_[read];
            consume(event);
            read = (read + 1U) % static_cast<std::uint32_t>(Capacity);
            __atomic_store_n(&read_index_, read, __ATOMIC_RELEASE);
            ++count;
        }
        return count;
    }

    std::uint32_t TakeDroppedCount() { return __atomic_exchange_n(&dropped_count_, 0U, __ATOMIC_ACQ_REL); }

private:
    std::array<Event, Capacity> events_{};
    std::uint32_t write_index_ = 0U;
    std::uint32_t read_index_ = 0U;
    std::uint32_t dropped_count_ = 0U;
};

} // namespace uai::ai::message_channel