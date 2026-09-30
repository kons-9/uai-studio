#pragma once

#include <cstdint>

#include <tk/tkernel.h>

#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"

namespace uai::ai::task {

/* Shared event-loop skeleton for long-lived µT-Kernel tasks. WaitForEvent
 * blocks or yields until work is available; ProcessOne handles one unit of
 * work. The measured interval excludes idle time in the wait callback. */
class Task final {
public:
    template <typename WaitForEvent, typename ProcessOne>
    [[noreturn]] static void RunForever(
        middleware::cpu_task_monitor::CpuTaskMonitor &monitor,
        const char *name, WaitForEvent wait_for_event, ProcessOne process_one)
    {
        const ID task_id = tk_get_tid();
        (void)monitor.RegisterTask(task_id, name);
        for (;;) {
            wait_for_event();
            const std::uint32_t loop_start = monitor.BeginTaskLoop();
            process_one();
            monitor.RecordTaskLoop(task_id, loop_start);
        }
    }
};

} // namespace uai::ai::task
