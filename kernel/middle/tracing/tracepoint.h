/*
 * tracepoint.h — eBPF-inspired tracepoint definitions for μT-Kernel 3.0
 *
 * Defines trace event types, the TraceEvent structure, and macros for
 * static / dynamic tracing. Configuration selects the mode:
 *
 *   UAI_TRACE_STATIC  — compile-time hooks, zero cost when disabled
 *   UAI_TRACE_DYNAMIC — runtime attach/detach via function pointers
 *
 * Each event carries: task ID, timestamp (μs), event type, and a
 * small payload union.
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace uai {

/* ------------------------------------------------------------------ */
/*  Trace event types                                                  */
/* ------------------------------------------------------------------ */

enum class TraceEventType : uint8_t {
    TASK_SWITCH       = 0x01,
    TASK_READY        = 0x02,
    TASK_WAIT         = 0x03,
    TASK_DORMANT      = 0x04,
    SEM_SIGNAL        = 0x10,
    SEM_WAIT          = 0x11,
    MTX_LOCK          = 0x12,
    MTX_UNLOCK        = 0x13,
    MEM_ALLOC         = 0x20,
    MEM_FREE          = 0x21,
    MEM_CORRUPTION    = 0x22,
    IRQ_ENTER         = 0x30,
    IRQ_EXIT          = 0x31,
    USER_EVENT        = 0x40,
};

/* ------------------------------------------------------------------ */
/*  Trace event payload                                                */
/* ------------------------------------------------------------------ */

struct TracePayload {
    union {
        /* TASK_SWITCH */
        struct { uint16_t prev_id; uint16_t next_id; } task_switch;
        /* SEM / MTX */
        struct { uint16_t resource_id; uint16_t waiter_id; } sync;
        /* MEM */
        struct { uint32_t address; uint16_t size; } mem;
        /* IRQ */
        struct { uint16_t irq_number; } irq;
        /* generic */
        uint8_t raw[8];
    };
};

/* ------------------------------------------------------------------ */
/*  Trace event (fixed 20 bytes — fits cache line efficiently)        */
/* ------------------------------------------------------------------ */

struct TraceEvent {
    uint32_t       timestamp_us;  /* DWT cycle counter / systick → μs  */
    uint16_t       task_id;       /* current task (tk_get_tid)          */
    TraceEventType type;           /* event category                    */
    uint8_t        flags;          /* reserved / filter tag             */
    TracePayload   payload;        /* event-specific data               */
    uint32_t       seq;            /* monotonic sequence number          */
};

static_assert(sizeof(TraceEvent) == 20, "TraceEvent must be 20 bytes");

/* ------------------------------------------------------------------ */
/*  Trace hook function pointer type (dynamic mode)                   */
/* ------------------------------------------------------------------ */

using TraceHookFn = void (*)(const TraceEvent &event);

/* ------------------------------------------------------------------ */
/*  Tracepoint IDs (index into hook array)                            */
/* ------------------------------------------------------------------ */

enum class TracepointId : uint8_t {
    TASK_SWITCH = 0,
    TASK_READY,
    TASK_WAIT,
    TASK_DORMANT,
    SEM_SIGNAL,
    SEM_WAIT,
    MTX_LOCK,
    MTX_UNLOCK,
    MEM_ALLOC,
    MEM_FREE,
    MEM_CORRUPTION,
    IRQ_ENTER,
    IRQ_EXIT,
    USER_EVENT,
    _COUNT   /* sentinel — number of tracepoints */
};

inline constexpr size_t TRACEPOINT_COUNT =
    static_cast<size_t>(TracepointId::_COUNT);

/* ------------------------------------------------------------------ */
/*  Platform timestamp helper (to be implemented per-platform)        */
/* ------------------------------------------------------------------ */

uint32_t trace_timestamp_us();   /* returns microsecond timestamp */
uint16_t trace_current_task();   /* returns current task ID       */

/* ------------------------------------------------------------------ */
/*  Static-trace macros                                                */
/* ------------------------------------------------------------------ */

#if defined(UAI_TRACE_STATIC) || defined(UAI_TRACE_DYNAMIC)

/* Forward — implemented in trace_hook.cpp */
void trace_emit(const TraceEvent &event);

#define UAI_TRACE_TASK_SWITCH(prev, next)                               \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::TASK_SWITCH;             \
        _ev.payload.task_switch.prev_id = (prev);                        \
        _ev.payload.task_switch.next_id = (next);                        \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_SEM_SIGNAL(sem_id)                                    \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::SEM_SIGNAL;              \
        _ev.payload.sync.resource_id = (sem_id);                         \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_SEM_WAIT(sem_id)                                      \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::SEM_WAIT;                \
        _ev.payload.sync.resource_id = (sem_id);                         \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_MTX_LOCK(mtx_id)                                      \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::MTX_LOCK;                \
        _ev.payload.sync.resource_id = (mtx_id);                         \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_MTX_UNLOCK(mtx_id)                                    \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::MTX_UNLOCK;              \
        _ev.payload.sync.resource_id = (mtx_id);                         \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_MEM_ALLOC(addr, sz)                                   \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::MEM_ALLOC;               \
        _ev.payload.mem.address = reinterpret_cast<uint32_t>(addr);      \
        _ev.payload.mem.size    = static_cast<uint16_t>(sz);             \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_MEM_FREE(addr, sz)                                    \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::MEM_FREE;                \
        _ev.payload.mem.address = reinterpret_cast<uint32_t>(addr);      \
        _ev.payload.mem.size    = static_cast<uint16_t>(sz);             \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_IRQ_ENTER(irq_num)                                    \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::IRQ_ENTER;               \
        _ev.payload.irq.irq_number = (irq_num);                         \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_IRQ_EXIT(irq_num)                                     \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::IRQ_EXIT;                \
        _ev.payload.irq.irq_number = (irq_num);                         \
        uai::trace_emit(_ev);                                            \
    } while (0)

#define UAI_TRACE_USER(tag)                                             \
    do {                                                                 \
        uai::TraceEvent _ev{};                                           \
        _ev.timestamp_us = uai::trace_timestamp_us();                    \
        _ev.task_id      = uai::trace_current_task();                    \
        _ev.type         = uai::TraceEventType::USER_EVENT;              \
        _ev.flags        = (tag);                                        \
        uai::trace_emit(_ev);                                            \
    } while (0)

#else /* tracing disabled */

#define UAI_TRACE_TASK_SWITCH(prev, next)  ((void)0)
#define UAI_TRACE_SEM_SIGNAL(sem_id)       ((void)0)
#define UAI_TRACE_SEM_WAIT(sem_id)         ((void)0)
#define UAI_TRACE_MTX_LOCK(mtx_id)         ((void)0)
#define UAI_TRACE_MTX_UNLOCK(mtx_id)       ((void)0)
#define UAI_TRACE_MEM_ALLOC(addr, sz)      ((void)0)
#define UAI_TRACE_MEM_FREE(addr, sz)       ((void)0)
#define UAI_TRACE_IRQ_ENTER(irq_num)       ((void)0)
#define UAI_TRACE_IRQ_EXIT(irq_num)        ((void)0)
#define UAI_TRACE_USER(tag)                ((void)0)

#endif

} // namespace uai
