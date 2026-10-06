#pragma once

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "driver/cache_driver/cache_driver.hpp"
#include "driver/camera_driver/camera_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/nor_driver/nor_driver.hpp"
#include "driver/psram_driver/psram_driver.hpp"
#include "driver/rif_driver/rif_driver.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"
#include "middleware/foundation/error.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "memory_manager/memory_manager.hpp"
#include "task/application_initialize_task.hpp"
#include "task/camera_render_task.hpp"
#include "task/pipeline_task.hpp"
#include "task/task_config.hpp"

namespace uai::ai::task {

/* References to the live tasks and resources shared by the application. */
class TaskContext final {
public:
    memory_manager::MemoryManager memory;
    ApplicationInitializeTask &application_task = ApplicationInitializeTask::Instance();
    CameraRenderTask &camera_task = CameraRenderTask::Instance();
    PipelineTask &pipeline_task = PipelineTask::Instance(memory);
    uai::ai::cache::CacheManagement &cache = cache::CacheManagement::Instance();
    uai::ai::psram::PsramManagement &psram = psram::PsramManagement::Instance();
    uai::ai::rif::RifManagement &rif = rif::RifManagement::Instance();
    uai::ai::lcd::LcdManagement &lcd = lcd::LcdManagement::Instance();
    uai::ai::camera::CameraManagement &camera = camera::CameraManagement::Instance();
    uai::ai::middleware::cpu_task_monitor::CpuTaskMonitor cpu_task_monitor;

    volatile std::uint32_t app_stage = 0U;
    volatile bool external_nor_ready = false;
    ID external_memory_ready = -1;
    ID pipeline_work_ready = -1;
    DiagnosticsConfig diagnostics;

    CameraRenderContext CameraContext()
    {
        return {memory, cache, camera, lcd, cpu_task_monitor, pipeline_task,
                external_nor_ready, diagnostics};
    }

    PipelineFrameContext FrameContext()
    {
        return {memory, cache, camera, cpu_task_monitor, pipeline_task,
            external_nor_ready, external_memory_ready,
                diagnostics};
    }

    PipelineWorkerContext WorkerContext()
    {
        return {cpu_task_monitor, pipeline_work_ready};
    }

    ApplicationInitializeContext InitializationContext()
    {
        return {memory, cache, psram, rif, lcd, camera, cpu_task_monitor,
                camera_task, pipeline_task, app_stage, external_nor_ready,
                external_memory_ready, diagnostics};
    }
};

inline TaskContext &GetTaskContext()
{
    static TaskContext context;
    return context;
}

} // namespace uai::ai::task
