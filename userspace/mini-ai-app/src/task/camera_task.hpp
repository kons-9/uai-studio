#pragma once

#include "app_config.hpp"
#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"

namespace uai::ai::mini {

/* Owns the camera and the LCD. Every loop it hands Pipe2 frames to the
 * inference task, picks up the newest result, and shows the Pipe1 frame with
 * the boxes drawn on top. It never waits for inference. */
class CameraTask final {
public:
    static CameraTask &Instance()
    {
        static CameraTask task;
        return task;
    }

    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);

private:
    static void Entry();
    [[noreturn]] void Run();
    common::StableAlignedBytes<kCameraTaskStackSize> stack_;
};

} // namespace uai::ai::mini
