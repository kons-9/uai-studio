#pragma once

#include <cstdint>

#include <tk/tkernel.h>

#include "middleware/buffer/stable_aligned_bytes.hpp"
#include "task/task_config.hpp"

namespace uai::ai::middleware::cpu_task_monitor { class CpuTaskMonitor; }
namespace uai::ai::memory_manager { class MemoryManager; }
namespace uai::ai::cache { class CacheManagement; }
namespace uai::ai::psram { class PsramManagement; }
namespace uai::ai::rif { class RifManagement; }
namespace uai::ai::lcd { class LcdManagement; }
namespace uai::ai::camera { class CameraManagement; }

namespace uai::ai::task {

class CameraRenderTask;
class PipelineTask;

struct ApplicationInitializeContext {
    memory_manager::MemoryManager &memory;
    cache::CacheManagement &cache;
    psram::PsramManagement &psram;
    rif::RifManagement &rif;
    lcd::LcdManagement &lcd;
    camera::CameraManagement &camera;
    middleware::cpu_task_monitor::CpuTaskMonitor &cpu_task_monitor;
    CameraRenderTask &camera_task;
    PipelineTask &pipeline_task;
    volatile std::uint32_t &app_stage;
    volatile bool &external_nor_ready;
    ID external_memory_ready;
    const DiagnosticsConfig &diagnostics;
};

class ApplicationInitializeTask final {
public:
    static ApplicationInitializeTask &Instance()
    {
        static ApplicationInitializeTask task;
        return task;
    }

    static void Entry();
    void Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor);

private:
    void Run(ApplicationInitializeContext context);
    common::StableAlignedBytes<kInitializationTaskStackSize> stack_;
};

} // namespace uai::ai::task
