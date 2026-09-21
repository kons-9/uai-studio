/*
 * stack_monitor.h — Stack canary monitoring for μAI-Studio tracing
 *
 * Integrates with the tracing infrastructure to check stack canaries
 * and emit MEM_CORRUPTION trace events on stack overflow detection.
 *
 * Usage:
 *   StackMonitor<256> monitor;
 *   monitor.register_task(task_id, stack_bottom_ptr);
 *   // Periodically (e.g., in idle task or timer):
 *   monitor.check_all();  // emits trace events on corruption
 */
#pragma once

#include "tracepoint.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace uai {

/* ------------------------------------------------------------------ */
/*  Stack canary monitor                                               */
/* ------------------------------------------------------------------ */

inline constexpr uint32_t STACK_CANARY_VALUE = 0xCAFEBABE;

template <size_t RingN = 256>
class StackMonitor {
public:
    static constexpr size_t MAX_MONITORED_TASKS = 16;

    struct TaskCanary {
        uint16_t           task_id;
        volatile uint32_t *stack_bottom;  /* lowest word of stack */
        bool               active;
        bool               corrupted;     /* already reported */
    };

    StackMonitor() { std::memset(canaries_, 0, sizeof(canaries_)); }

    /**
     * Register a task's stack for canary monitoring.
     * Writes STACK_CANARY_VALUE to *stack_bottom.
     * Returns 0 on success, negative on error.
     */
    int register_task(uint16_t task_id, volatile uint32_t *stack_bottom)
    {
        if (!stack_bottom) return -1;

        /* Update existing or find free slot */
        int free_slot = -1;
        for (size_t i = 0; i < MAX_MONITORED_TASKS; i++) {
            if (canaries_[i].active && canaries_[i].task_id == task_id) {
                canaries_[i].stack_bottom = stack_bottom;
                canaries_[i].corrupted = false;
                *stack_bottom = STACK_CANARY_VALUE;
                return 0;
            }
            if (!canaries_[i].active && free_slot < 0) {
                free_slot = static_cast<int>(i);
            }
        }

        if (free_slot < 0) return -2;  /* no free slots */

        canaries_[free_slot].task_id = task_id;
        canaries_[free_slot].stack_bottom = stack_bottom;
        canaries_[free_slot].active = true;
        canaries_[free_slot].corrupted = false;
        *stack_bottom = STACK_CANARY_VALUE;
        n_tasks_++;
        return 0;
    }

    /**
     * Unregister a task from canary monitoring.
     */
    int unregister_task(uint16_t task_id)
    {
        for (size_t i = 0; i < MAX_MONITORED_TASKS; i++) {
            if (canaries_[i].active && canaries_[i].task_id == task_id) {
                canaries_[i].active = false;
                n_tasks_--;
                return 0;
            }
        }
        return -1;
    }

    /**
     * Check all registered canaries. Returns number of corruptions found.
     * Emits a TraceEvent (MEM_CORRUPTION) for each newly-detected corruption
     * via the provided emit function.
     */
    using EmitFn = void (*)(const TraceEvent &);

    size_t check_all(EmitFn emit_fn = nullptr)
    {
        size_t errors = 0;

        for (size_t i = 0; i < MAX_MONITORED_TASKS; i++) {
            if (!canaries_[i].active) continue;
            if (canaries_[i].corrupted) continue;  /* already reported */

            if (*canaries_[i].stack_bottom != STACK_CANARY_VALUE) {
                canaries_[i].corrupted = true;
                errors++;

                if (emit_fn) {
                    TraceEvent ev{};
                    ev.timestamp_us = trace_timestamp_us();
                    ev.task_id = canaries_[i].task_id;
                    ev.type = TraceEventType::MEM_CORRUPTION;
                    ev.flags = 0x05;  /* STACK_OVERFLOW subtype */
                    ev.payload.mem.address =
                        reinterpret_cast<uint32_t>(canaries_[i].stack_bottom);
                    ev.payload.mem.size = 0;
                    emit_fn(ev);
                }
            }
        }

        return errors;
    }

    /**
     * Reset corruption flags so corrupted tasks can be re-detected
     * after the canary is re-written.
     */
    void reset_corruption_flags()
    {
        for (size_t i = 0; i < MAX_MONITORED_TASKS; i++) {
            canaries_[i].corrupted = false;
        }
    }

    size_t task_count() const { return n_tasks_; }

    const TaskCanary *get_canary(size_t idx) const
    {
        if (idx >= MAX_MONITORED_TASKS) return nullptr;
        if (!canaries_[idx].active) return nullptr;
        return &canaries_[idx];
    }

private:
    TaskCanary canaries_[MAX_MONITORED_TASKS]{};
    size_t     n_tasks_ = 0;
};

} // namespace uai
