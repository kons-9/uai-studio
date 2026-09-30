#include "task/application_initialize_task.hpp"

#include "driver/npu_driver/debug.h"
#include "common/log.hpp"
#include "task/task_context.hpp"
#include "task/camera_render_task.hpp"
#include "task/pipeline_task.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>
}

namespace uai::ai::task {

namespace {
constexpr std::uintptr_t kModelDataAddress = 0x70380000UL;
constexpr const char *kModelDataAddressName = "70380000";
} // namespace

void ApplicationInitializeTask::Entry()
{
    ApplicationInitializeTask{}.Run();
}

void ApplicationInitializeTask::Run()
{
    TaskContext &context = GetTaskContext();
    /* Resume the nominal HAL tick after pre-kernel setup. The sample-ai HAL
     * time bridge uses µT-Kernel time for HAL_GetTick/HAL_Delay. */
    HAL_ResumeTick();
    context.ConfigureReferenceInterruptPriorities();
    context.app_stage = 1U;
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "boot: external memory init begin\n"));

    const common::Error driver_status = context.InitializeDrivers();
    if (!driver_status.Ok()) {
        LogStatus("driver", driver_status);
        context.Halt("ai: driver initialization failed\n");
    }
    /* The XSPI NOR driver emits a long register snapshot on failure.
     * Keep other tasks from writing to the same T-Monitor UART while that
     * snapshot is being transferred, otherwise the diagnostic lines become
     * interleaved and unreadable.  Interrupts remain enabled, so HAL tick
     * timeouts used by the BSP continue to work. */
    if constexpr (kInferenceMode == InferenceMode::kNpu) {
        if (!context.external_nor_ready) {
            UAI_LOG_WARN(reinterpret_cast<const UB *>(
                             "ai: external NOR unavailable status=%d; inference disabled\n"),
                         static_cast<int>(driver_status.detail));
        }
    }
    const common::Error cpu_trace_status =
        context.cpu_task_monitor.InitializeTraceBuffer();
    if (!cpu_trace_status.Ok()) {
        LogStatus("cpu_task_monitor.trace", cpu_trace_status);
    }
    context.app_stage = 2U;
    context.app_stage = 3U;
    context.app_stage = 4U;
    if (context.diagnostics.register_dump) {
        DumpPeripheralRegisters("after_access");
        UAI_LOG_DEBUG(reinterpret_cast<const UB *>(
                          "boot: npu cache init=%x enable=%x invalidate=%x cr1=%x sr=%x\n"),
                      g_npu_cache_init_status, g_npu_cache_enable_status,
                      g_npu_cache_invalidate_status, g_npu_cache_cr1,
                      g_npu_cache_sr);
    }
    UAI_LOG_INFO(reinterpret_cast<const UB *>(
                     "boot: external memory init result=ok detail=0\n"));
    if (context.external_nor_ready) {
        const volatile std::uint32_t *model_data =
            reinterpret_cast<const volatile std::uint32_t *>(kModelDataAddress);
        UAI_LOG_DEBUG(reinterpret_cast<const UB *>(
                          "boot: model data @%s=%x,%x,%x,%x\n"),
                      reinterpret_cast<const UB *>(
                          const_cast<char *>(kModelDataAddressName)),
                      static_cast<unsigned int>(model_data[0]),
                      static_cast<unsigned int>(model_data[1]),
                      static_cast<unsigned int>(model_data[2]),
                      static_cast<unsigned int>(model_data[3]));
    } else {
        UAI_LOG_WARN(reinterpret_cast<const UB *>(
                         "boot: model data read skipped; NOR is not mapped\n"));
    }
    (void)tk_set_flg(context.external_memory_ready, kExternalMemoryReady);

    /* Keep the hardware initialization ahead of both application tasks, but
     * do the work on a dedicated stack rather than the small µT-Kernel
     * initial-task stack. */
    context.StartCameraTask(reinterpret_cast<FP>(CameraRenderTask::Entry));
    context.app_stage = 5U;
    if constexpr (kInferenceMode == InferenceMode::kNpu) {
        context.StartFrameTask(
            reinterpret_cast<FP>(PipelineTask::FrameEntry));
    } else if constexpr (kInferenceMode == InferenceMode::kCopyOnly) {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: copy-only snapshot mode; NPU task disabled\n"));
    } else {
        UAI_LOG_INFO(reinterpret_cast<const UB *>(
                         "ai: inference task disabled for camera/CSI isolation\n"));
    }
    context.app_stage = 6U;

    for (;;) {
        tk_dly_tsk(1000);
        context.cpu_task_monitor.Report();
    }
}

} // namespace uai::ai::task
