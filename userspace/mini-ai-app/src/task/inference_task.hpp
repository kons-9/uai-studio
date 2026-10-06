#pragma once

#include "app_config.hpp"
#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"

namespace uai::ai::mini {

/* Runs the person model on each Pipe2 frame it receives: invalidate the
 * input, run the NPU (blocking), decode the outputs on the CPU, publish the
 * boxes, and return the frame buffer. One frame at a time. */
class InferenceTask final {
public:
    static InferenceTask &Instance()
    {
        static InferenceTask task;
        return task;
    }

    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);

private:
    static void Entry();
    [[noreturn]] void Run();
    common::StableAlignedBytes<kInferenceTaskStackSize> stack_;
};

} // namespace uai::ai::mini
