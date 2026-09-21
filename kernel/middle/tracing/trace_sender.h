/*
 * trace_sender.h — Drains ring buffer and sends events over transport
 *
 * Runs periodically (e.g. in a low-priority task) to flush buffered
 * trace events to the PC via the transport layer.
 *
 * Wire format per event (20 bytes, little-endian):
 *   magic(2) + msg_type(1) + TraceEvent(20) = 23 bytes per frame
 */
#pragma once

#include "trace_hook.h"
#include "tracepoint.h"

#include <cstdint>
#include <cstring>

namespace uai {

inline constexpr uint8_t MSG_TRACE_EVENT = 0x20;
inline constexpr size_t  TRACE_FRAME_SIZE = 2 + 1 + sizeof(TraceEvent);

template <typename Transport, size_t RingN = 256>
class TraceSender {
public:
    TraceSender(Transport &transport, TraceEngine<RingN> &engine)
        : transport_(transport), engine_(engine)
    {}

    /* Drain up to batch_size events and send. Returns events sent. */
    int flush(size_t batch_size = 16)
    {
        TraceEvent events[16];
        size_t max = (batch_size > 16) ? 16 : batch_size;
        size_t n = engine_.ring().drain(events, max);

        for (size_t i = 0; i < n; i++) {
            uint8_t frame[TRACE_FRAME_SIZE];
            encode_frame(events[i], frame);

            int rc = transport_.send(frame, TRACE_FRAME_SIZE);
            if (rc < 0) return rc;
            sent_count_++;
        }
        return static_cast<int>(n);
    }

    uint32_t sent_count() const { return sent_count_; }

private:
    void encode_frame(const TraceEvent &ev, uint8_t *out)
    {
        /* magic */
        out[0] = 0x55; out[1] = 0xAB; /* PROTO_MAGIC LE */
        out[2] = MSG_TRACE_EVENT;
        std::memcpy(&out[3], &ev, sizeof(TraceEvent));
    }

    Transport          &transport_;
    TraceEngine<RingN> &engine_;
    uint32_t            sent_count_ = 0;
};

} // namespace uai
