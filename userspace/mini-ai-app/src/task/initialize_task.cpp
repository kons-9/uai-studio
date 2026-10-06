#include "task/initialize_task.hpp"

#include <tk/tkernel.h>

extern "C" {
#include "stm32n6xx_hal.h"
}

#include "app_context.hpp"
#include "driver/board/interrupt_priority.hpp"
#include "driver/npu_driver/npu_driver.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/task/task.hpp"
#include "task/camera_task.hpp"
#include "task/inference_task.hpp"

namespace uai::ai::mini {

namespace {

bool Initialized(const common::Error &status)
{
    return status.Ok() ||
           status.Code() == common::ErrorCode::kAlreadyInitialized;
}

/* The order matters: RIF opens the external memories, PSRAM resets XSPIM
 * before NOR is mapped, and the LCD and camera need the buffer pools. */
common::Error InitializeDrivers(AppContext &app)
{
    common::Error status = npu::NpuDriver::InitializeMemory();
    if (!Initialized(status)) return status;

    status = app.cache.Initialize();
    if (!Initialized(status)) return status;

    status = app.memory.Initialize();
    if (!status.Ok()) return status;

    status = app.rif.Initialize();
    if (!Initialized(status)) return status;

    if (!app.psram.Initialize()) return {common::ErrorCode::kHardware};

    app.external_nor_ready = app.nor.Initialize() == 0;
    if (!app.external_nor_ready) {
        UAI_LOG_WARN("mini: NOR unavailable; inference disabled\n");
    }

    status = app.lcd.Initialize(app.memory, app.cache);
    if (!Initialized(status)) return status;

    status = app.camera.Initialize(app.memory, app.cache);
    if (!Initialized(status)) return status;

    /* Tasks sleep between frames; keep the peripheral clocks running. */
    app.cache.KeepClocksOnSleep();
    app.psram.KeepClocksOnSleep();
    if (app.external_nor_ready) {
        nor::NorManagement::Accessor nor_accessor;
        status = app.nor.Acquire(&nor_accessor);
        if (!status.Ok()) return status;
        nor_accessor.KeepClocksOnSleep();
    }
    npu::NpuDriver::KeepMemoryClocksOnSleep();
    app.lcd.KeepClocksOnSleep();
    app.camera.KeepClocksOnSleep();
    return {};
}

} // namespace

void InitializeTask::Start(
    middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_,
                        kInitializeTaskPriority, "initialize");
}

void InitializeTask::Entry() { Instance().Run(); }

void InitializeTask::Run()
{
    AppContext &app = App();

    /* The pre-kernel startup paused the HAL tick; µT-Kernel now provides it. */
    HAL_ResumeTick();
    driver::board::ConfigureReferenceInterruptPriorities();

    UAI_LOG_INFO("mini: driver init begin\n");
    const common::Error status = InitializeDrivers(app);
    if (!status.Ok()) {
        status.LogStatus("driver");
        common::Task::Halt("mini: driver initialization failed\n");
    }
    UAI_LOG_INFO("mini: driver init done nor=%u\n",
                 static_cast<unsigned int>(app.external_nor_ready));

    const common::Error trace_status =
        app.cpu_task_monitor.InitializeTraceBuffer();
    if (!trace_status.Ok()) trace_status.LogStatus("cpu_task_monitor.trace");

    (void)tk_set_flg(app.external_memory_ready, kExternalMemoryReady);

    CameraTask::Instance().Start(app.cpu_task_monitor);
    InferenceTask::Instance().Start(app.cpu_task_monitor);

    common::Task::RunForever(app.cpu_task_monitor, "initialize",
                             [] { tk_dly_tsk(1000); },
                             [&] { app.cpu_task_monitor.Report(); });
}

} // namespace uai::ai::mini
