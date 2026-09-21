/*
 * ring_buffer.h — Lock-free SPSC ring buffer for trace events
 *
 * Single-Producer Single-Consumer design suitable for ISR → task
 * or task → DMA transfer. Uses acquire/release memory ordering
 * on volatile indices.
 *
 * Template parameter N must be a power of 2 for efficient masking.
 */
#pragma once

#include "tracepoint.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace uai {

template <size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

public:
    RingBuffer() = default;

    /* Producer: push one event. Returns false if full. */
    bool push(const TraceEvent &event)
    {
        const uint32_t head = head_;
        const uint32_t next = (head + 1) & MASK;

        if (next == tail_) {
            overflows_++;
            return false; /* buffer full */
        }

        buf_[head] = event;
        /* release store: ensure event data is visible before head advances */
        head_ = next;
        return true;
    }

    /* Consumer: pop one event. Returns false if empty. */
    bool pop(TraceEvent &out)
    {
        const uint32_t tail = tail_;
        if (tail == head_) {
            return false; /* empty */
        }

        out = buf_[tail];
        /* release store: ensure read completes before tail advances */
        tail_ = (tail + 1) & MASK;
        return true;
    }

    /* Number of events currently buffered */
    uint32_t count() const
    {
        return (head_ - tail_) & MASK;
    }

    bool empty() const { return head_ == tail_; }
    bool full() const  { return ((head_ + 1) & MASK) == tail_; }

    static constexpr size_t capacity() { return N - 1; }

    uint32_t overflows() const { return overflows_; }
    void     reset_overflows() { overflows_ = 0; }

    void clear()
    {
        head_ = 0;
        tail_ = 0;
        overflows_ = 0;
    }

    /* Drain up to max_count events into output array. Returns count drained. */
    size_t drain(TraceEvent *out, size_t max_count)
    {
        size_t i = 0;
        while (i < max_count && pop(out[i])) {
            i++;
        }
        return i;
    }

private:
    static constexpr uint32_t MASK = N - 1;

    TraceEvent      buf_[N]{};
    volatile uint32_t head_      = 0;
    volatile uint32_t tail_      = 0;
    uint32_t          overflows_ = 0;
};

/* Recommended sizes for MCU (RAM-constrained) */
using TraceRingSmall  = RingBuffer<64>;    /* 64 × 20 = 1,280 bytes */
using TraceRingMedium = RingBuffer<256>;   /* 256 × 20 = 5,120 bytes */
using TraceRingLarge  = RingBuffer<1024>;  /* 1024 × 20 = 20,480 bytes */

} // namespace uai
