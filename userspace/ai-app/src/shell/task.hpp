#pragma once

#include <tk/tkernel.h>

#include "middleware/buffer/stable_aligned_bytes.hpp"

namespace uai::ai::middleware::cpu_task_monitor {
class CpuTaskMonitor;
}
namespace uai::ai::shell {
class Mailbox;

class ShellTask final {
public:
    static ShellTask &Instance()
    {
        static ShellTask task;
        return task;
    }

    static void Entry();
    static void Wake(void *context);
    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor, ID &wake_flag);
    void Run(Mailbox &mailbox, ID wake_flag, middleware::cpu_task_monitor::CpuTaskMonitor &monitor);

private:
    common::StableAlignedBytes<8192U> stack_;
};

} // namespace uai::ai::shell