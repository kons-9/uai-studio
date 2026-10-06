#pragma once

#include "app_config.hpp"
#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"

namespace uai::ai::mini {

/* Brings up the drivers in dependency order, then starts the camera and
 * inference tasks. Runs on its own stack because driver initialization is
 * too deep for the µT-Kernel initial task. */
class InitializeTask final {
public:
    static InitializeTask &Instance()
    {
        static InitializeTask task;
        return task;
    }

    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);

private:
    static void Entry();
    [[noreturn]] void Run();
    common::StableAlignedBytes<kInitializeTaskStackSize> stack_;
};

} // namespace uai::ai::mini
