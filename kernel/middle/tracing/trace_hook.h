/*
 * trace_hook.h — Dynamic hook infrastructure for tracepoints
 *
 * In UAI_TRACE_DYNAMIC mode, each tracepoint has a slot for a
 * user-provided hook function. Hooks can be attached/detached at
 * runtime from PC side via MCP or from application code.
 *
 * When no hook is attached the overhead is a single NULL-pointer
 * check (1–2 cycles). With a hook the overhead is ~50–200 cycles
 * depending on the hook body.
 */
#pragma once

#include "tracepoint.h"
#include "ring_buffer.h"

#include <cstdint>

namespace uai {

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

struct TraceConfig {
    bool     dynamic_enabled;       /* true = dynamic hooks active      */
    uint32_t ring_size;             /* hint (actual size is template)   */
};

/* ------------------------------------------------------------------ */
/*  TraceEngine — central trace infrastructure                        */
/* ------------------------------------------------------------------ */

template <size_t RingN = 256>
class TraceEngine {
public:
    explicit TraceEngine(const TraceConfig &config = {})
        : dynamic_enabled_(config.dynamic_enabled)
    {}

    /* ---- ring buffer access ---- */
    RingBuffer<RingN>       &ring()       { return ring_; }
    const RingBuffer<RingN> &ring() const { return ring_; }

    /* ---- emit an event (called by trace macros) ---- */
    void emit(const TraceEvent &event)
    {
        /* Assign sequence number */
        TraceEvent ev = event;
        ev.seq = seq_++;

#ifdef UAI_TRACE_DYNAMIC
        /* Run attached hook (if any) */
        auto tp = static_cast<size_t>(event_to_tracepoint(event.type));
        if (tp < TRACEPOINT_COUNT && hooks_[tp]) {
            hooks_[tp](ev);
        }

        /* Run filter (mini-VM) — if filter returns false, drop event */
        if (filter_fn_ && !filter_fn_(ev)) {
            filtered_++;
            return;
        }
#endif
        /* Push into ring buffer */
        ring_.push(ev);
    }

    /* ---- dynamic hook management (UAI_TRACE_DYNAMIC) ---- */
#ifdef UAI_TRACE_DYNAMIC
    int attach(TracepointId tp, TraceHookFn hook)
    {
        auto idx = static_cast<size_t>(tp);
        if (idx >= TRACEPOINT_COUNT) return -1;
        hooks_[idx] = hook;
        return 0;
    }

    int detach(TracepointId tp)
    {
        auto idx = static_cast<size_t>(tp);
        if (idx >= TRACEPOINT_COUNT) return -1;
        hooks_[idx] = nullptr;
        return 0;
    }

    void detach_all()
    {
        for (size_t i = 0; i < TRACEPOINT_COUNT; i++) {
            hooks_[i] = nullptr;
        }
    }

    /* Filter function type: returns true to keep event, false to drop */
    using FilterFn = bool (*)(const TraceEvent &);

    void set_filter(FilterFn fn) { filter_fn_ = fn; }
    void clear_filter()          { filter_fn_ = nullptr; }

    uint32_t filtered_count() const { return filtered_; }
#endif

    /* ---- statistics ---- */
    uint32_t event_count() const { return seq_; }

    void reset()
    {
        ring_.clear();
        seq_ = 0;
#ifdef UAI_TRACE_DYNAMIC
        detach_all();
        filter_fn_ = nullptr;
        filtered_ = 0;
#endif
    }

private:
    /* Map event type to tracepoint ID */
    static TracepointId event_to_tracepoint(TraceEventType type)
    {
        switch (type) {
        case TraceEventType::TASK_SWITCH:    return TracepointId::TASK_SWITCH;
        case TraceEventType::TASK_READY:     return TracepointId::TASK_READY;
        case TraceEventType::TASK_WAIT:      return TracepointId::TASK_WAIT;
        case TraceEventType::TASK_DORMANT:   return TracepointId::TASK_DORMANT;
        case TraceEventType::SEM_SIGNAL:     return TracepointId::SEM_SIGNAL;
        case TraceEventType::SEM_WAIT:       return TracepointId::SEM_WAIT;
        case TraceEventType::MTX_LOCK:       return TracepointId::MTX_LOCK;
        case TraceEventType::MTX_UNLOCK:     return TracepointId::MTX_UNLOCK;
        case TraceEventType::MEM_ALLOC:      return TracepointId::MEM_ALLOC;
        case TraceEventType::MEM_FREE:       return TracepointId::MEM_FREE;
        case TraceEventType::MEM_CORRUPTION: return TracepointId::MEM_CORRUPTION;
        case TraceEventType::IRQ_ENTER:      return TracepointId::IRQ_ENTER;
        case TraceEventType::IRQ_EXIT:       return TracepointId::IRQ_EXIT;
        case TraceEventType::USER_EVENT:     return TracepointId::USER_EVENT;
        default:                             return TracepointId::_COUNT;
        }
    }

    RingBuffer<RingN> ring_;
    uint32_t          seq_ = 0;
    bool              dynamic_enabled_ = false;

#ifdef UAI_TRACE_DYNAMIC
    TraceHookFn hooks_[TRACEPOINT_COUNT]{};
    FilterFn    filter_fn_ = nullptr;
    uint32_t    filtered_  = 0;
#endif
};

/* Default engine type for typical MCU (5 KB ring buffer) */
using DefaultTraceEngine = TraceEngine<256>;

/* ------------------------------------------------------------------ */
/*  Global trace engine access (set by application init)              */
/* ------------------------------------------------------------------ */

void          trace_engine_init(DefaultTraceEngine *engine);
DefaultTraceEngine *trace_engine_get();

} // namespace uai
