#pragma once

#include <cstdint>

#include <tk/tkernel.h>

#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "task/task_config.hpp"
#include "task/camera_render_wake.hpp"

namespace uai::ai::middleware::cpu_task_monitor {
class CpuTaskMonitor;
}
namespace uai::ai::memory_manager {
class MemoryManager;
}
namespace uai::ai::cache {
class CacheManagement;
}
namespace uai::ai::camera {
class CameraManagement;
}
namespace uai::ai::lcd {
class LcdManagement;
}
namespace uai::ai::touch {
class TouchManagement;
}
namespace uai::ai::shell {
class Mailbox;
}

namespace uai::ai::task {

class PipelineTask;

struct CameraRenderContext {
    memory_manager::MemoryManager &memory;
    cache::CacheManagement &cache;
    camera::CameraManagement &camera;
    lcd::LcdManagement &lcd;
    touch::TouchManagement &touch;
    middleware::cpu_task_monitor::CpuTaskMonitor &cpu_task_monitor;
    PipelineTask &pipeline_task;
    const volatile bool &external_nor_ready;
    const volatile bool &touch_ready;
    DiagnosticsConfig &diagnostics;
    shell::Mailbox &shell_mailbox;
};

class CameraRenderTask final {
public:
    static CameraRenderTask &Instance()
    {
        static CameraRenderTask task;
        return task;
    }

    static void Entry();
    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);

private:
    void Run(CameraRenderContext context);
    CameraRenderWake wake_;
    common::StableAlignedBytes<kCameraTaskStackSize> stack_;
};

} // namespace uai::ai::task
