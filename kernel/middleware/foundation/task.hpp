#pragma once

#include <cstdint>
#include <limits>

#include <tk/tkernel.h>

#include "middleware/foundation/error.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/foundation/stable_aligned_bytes.hpp"

namespace uai::ai::common {

class Task final {
public:
    [[noreturn]] static void Halt(const char *message)
    {
        UAI_LOG_TEXT(LogLevel::kError, message);
        for (;;) tk_dly_tsk(1000);
    }

    static std::uint32_t Now()
    {
        SYSTIM time{};
        return tk_get_otm(&time) == E_OK ? time.lo : 0U;
    }

    template <typename Monitor, std::size_t StackBytes>
    static void Start(Monitor &monitor,
                      FP entry, StableAlignedBytes<StackBytes> &stack, PRI priority,
                      const char *name)
    {
        static_assert(StackBytes >= 128U);
        static_assert(StackBytes <= std::numeric_limits<SZ>::max());
        T_CTSK task{};
        task.tskatr = TA_HLNG | TA_USERBUF;
        task.task = entry;
        task.itskpri = priority;
        task.stksz = static_cast<SZ>(stack.size_bytes());
        task.bufptr = stack.data();
        const ID task_id = tk_cre_tsk(&task);
        if (task_id < E_OK) {
            UAI_LOG_ERROR("error: component=task_context operation=create_%s code=%x detail=0\n",
                          name, static_cast<unsigned int>(task_id));
            Halt("ai: task create failed\n");
        }
        const Error status = monitor.RegisterTask(task_id, name);
        if (!status.Ok()) {
            status.LogStatus(name, LogLevel::kWarn);
        }
        const ER error = tk_sta_tsk(task_id, 0);
        if (error != E_OK) {
            UAI_LOG_ERROR("error: component=task_context operation=start_%s code=%x detail=0\n",
                          name, static_cast<unsigned int>(error));
            Halt("ai: task start failed\n");
        }
    }

    template <typename Monitor, typename WaitForEvent, typename ProcessOne>
    [[noreturn]] static void RunForever(
        Monitor &monitor,
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

} // namespace uai::ai::common