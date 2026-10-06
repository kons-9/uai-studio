#include "task/application_initialize_task.hpp"

#include "driver/npu_driver/debug.h"
#include "driver/npu_driver/npu_driver.hpp"
#include "driver/board/register_diagnostics.hpp"
#include "driver/board/interrupt_priority.hpp"
#include "middleware/foundation/log.hpp"
#include "task/task_context.hpp"
#include "middleware/foundation/task.hpp"
#include "task/camera_render_task.hpp"
#include "task/pipeline_task.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task {

namespace {
constexpr std::uintptr_t kModelDataAddress = 0x70380000UL;
constexpr const char *kModelDataAddressName = "70380000";

common::Error InitializeDrivers(ApplicationInitializeContext &context)
{
    common::Error status = npu::NpuDriver::InitializeMemory();
    if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = context.cache.Initialize();
    if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    status = context.memory.Initialize();
    if (!status.Ok()) return status;

    status = context.rif.Initialize();
    if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }

    /* XSPI1/XSPI2 are RIF-protected on a cold boot. */
    if (!context.psram.Initialize()) {
        return {common::ErrorCode::kHardware};
    }

    constexpr bool initialize_nor = kInferenceMode == InferenceMode::kNpu;
    int nor_status = -1;
    if (initialize_nor) {
        nor_status = nor::NorManagement::Instance().Initialize();
    } else {
        UAI_LOG_INFO("boot: NOR skipped: inference disabled\n");
    }
    context.external_nor_ready = nor_status == 0;

    status = context.lcd.Initialize(context.memory, context.cache);
    if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }
    context.lcd.SetTimingDiagnostics(context.diagnostics.display_timing);
    status = context.camera.Initialize(context.memory, context.cache);
    if (!status.Ok() && status.Code() != common::ErrorCode::kAlreadyInitialized) {
        return status;
    }

    context.cache.KeepClocksOnSleep();
    context.psram.KeepClocksOnSleep();
    if (context.external_nor_ready) {
        nor::NorManagement::Accessor nor_accessor;
        status = nor::NorManagement::Instance().Acquire(&nor_accessor);
        if (!status.Ok()) return status;
        nor_accessor.KeepClocksOnSleep();
    }
    npu::NpuDriver::KeepMemoryClocksOnSleep();
    context.lcd.KeepClocksOnSleep();
    context.camera.KeepClocksOnSleep();
    return {common::ErrorCode::kOk};
}
} // namespace

void ApplicationInitializeTask::Entry()
{
    TaskContext &root = GetTaskContext();
    root.application_task.Run(root.InitializationContext());
}

void ApplicationInitializeTask::Start(
    middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_,
                5, "application_initialize");
}

void ApplicationInitializeTask::Run(ApplicationInitializeContext context)
{
    /* Resume the nominal HAL tick after pre-kernel setup. The experiment-ai HAL
     * time bridge uses µT-Kernel time for HAL_GetTick/HAL_Delay. */
    HAL_ResumeTick();
    driver::board::ConfigureReferenceInterruptPriorities();
    context.app_stage = 1U;
    UAI_LOG_INFO("boot: external memory init begin\n");

    const common::Error driver_status = InitializeDrivers(context);
    if (!driver_status.Ok()) {
        driver_status.LogStatus("driver");
        common::Task::Halt("ai: driver initialization failed\n");
    }
    /* The XSPI NOR driver emits a long register snapshot on failure.
     * Keep other tasks from writing to the same T-Monitor UART while that
     * snapshot is being transferred, otherwise the diagnostic lines become
     * interleaved and unreadable.  Interrupts remain enabled, so HAL tick
     * timeouts used by the BSP continue to work. */
    if constexpr (kInferenceMode == InferenceMode::kNpu) {
        if (!context.external_nor_ready) {
            UAI_LOG_WARN("ai: external NOR unavailable; inference disabled\n");
        }
    }
    const common::Error cpu_trace_status =
        context.cpu_task_monitor.InitializeTraceBuffer();
    if (!cpu_trace_status.Ok()) {
        cpu_trace_status.LogStatus("cpu_task_monitor.trace");
    }
    context.app_stage = 2U;
    context.app_stage = 3U;
    context.app_stage = 4U;
    if (context.diagnostics.register_dump) {
        driver::board::DumpPeripheralRegisters("after_access");
        UAI_LOG_DEBUG("boot: npu cache init=%x enable=%x invalidate=%x cr1=%x sr=%x\n",
                      g_npu_cache_init_status, g_npu_cache_enable_status,
                      g_npu_cache_invalidate_status, g_npu_cache_cr1,
                      g_npu_cache_sr);
    }
    UAI_LOG_INFO("boot: external memory init result=ok detail=0\n");
    if (context.external_nor_ready) {
        const volatile std::uint32_t *model_data =
            reinterpret_cast<const volatile std::uint32_t *>(kModelDataAddress);
        UAI_LOG_DEBUG("boot: model data @%s=%x,%x,%x,%x\n",
                      kModelDataAddressName,
                      static_cast<unsigned int>(model_data[0]),
                      static_cast<unsigned int>(model_data[1]),
                      static_cast<unsigned int>(model_data[2]),
                      static_cast<unsigned int>(model_data[3]));
    } else {
        UAI_LOG_WARN("boot: model data read skipped; NOR is not mapped\n");
    }
    (void)tk_set_flg(context.external_memory_ready, kExternalMemoryReady);

    /* Keep the hardware initialization ahead of both application tasks, but
     * do the work on a dedicated stack rather than the small µT-Kernel
     * initial-task stack. */
    context.camera_task.Start(context.cpu_task_monitor);
    context.app_stage = 5U;
    if constexpr (kInferenceMode == InferenceMode::kNpu) {
        context.pipeline_task.StartFrame(context.cpu_task_monitor);
    } else if constexpr (kInferenceMode == InferenceMode::kCopyOnly) {
        UAI_LOG_INFO("ai: copy-only snapshot mode; NPU task disabled\n");
    } else {
        UAI_LOG_INFO("ai: inference task disabled for camera/CSI isolation\n");
    }
    context.app_stage = 6U;

    common::Task::RunForever(context.cpu_task_monitor, "application_initialize",
                     [] { tk_dly_tsk(1000); },
                     [&] { context.cpu_task_monitor.Report(); });
}

} // namespace uai::ai::task
