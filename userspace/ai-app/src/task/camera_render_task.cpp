#include <cstdint>

#include <tk/tkernel.h>

#include "driver/camera_driver/camera_driver.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "task/camera_render/camera_render_context_and_operations.hpp"
#include "task/camera_render_task.hpp"
#include "task/camera_render_state.hpp"
#include "middleware/task/task.hpp"
#include "task/task_context.hpp"
#include "ui/app_ui.hpp"

namespace uai::ai::task {

void CameraRenderTask::Entry()
{
    TaskContext &root = GetTaskContext();
    root.camera_task.Run(root.CameraContext());
}

void CameraRenderTask::Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_, 5, "camera_render");
}

void CameraRenderTask::Run(CameraRenderContext context)
{
    common::Error status{};
    status = wake_.Create();
    if (status.Ok())
        status = context.camera.SetNotifications(
            {wake_.Bind(CameraRenderWake::kCapture),
             wake_.Bind(CameraRenderWake::kPipe2),
             wake_.Bind(CameraRenderWake::kVsync),
             wake_.Bind(CameraRenderWake::kError)}
        );
    if (!status.Ok()) {
        status.LogStatus("camera: wake registration");
        common::Task::Halt("ai: camera wake registration failed\n");
    }
    context.pipeline_task.SetResultNotification(wake_.Bind(CameraRenderWake::kResult));
    context.shell_mailbox.SetNotification(wake_.Bind(CameraRenderWake::kRequest));

    const inference::BoxSet initial{};
    camera_render::ShowInitialDisplayAndStartCamera(context, initial);

    CameraRenderState state(context.pipeline_task, initial, common::Task::Now());
    status = context.camera.GetGeometry(&state.geometry);
    if (!status.Ok())
        common::Task::Halt("ai: camera geometry unavailable\n");
    UAI_LOG_INFO(
        "ui: touch=%s screens=%u\n",
        context.touch_ready ? "ready" : "disabled",
        static_cast<unsigned int>(app_ui::kScreenCount)
    );

    state.diagnostics = CameraDiagnosticState(common::Task::Now());
    UINT wake_reasons = 0U;
    camera_render::SyntheticFaceOutputState synthetic_face;
    common::Task::RunForever(
        context.cpu_task_monitor,
        "camera_render",
        [&] {
            const auto now = common::Task::Now();
            auto wait = state.RemainingWait(now, context.touch_ready);
            std::uint32_t service_wait = UINT32_MAX;
            const auto service_status = context.camera.GetServiceWait(&service_wait);
            if (!service_status.Ok())
                common::Task::Halt("ai: camera service deadline failed\n");
            if (service_wait < wait)
                wait = service_wait;
            if (context.shell_mailbox.Pending())
                wait = 0U;
            const auto wake_status = wake_.Wait(wait, &wake_reasons);
            if (!wake_status.Ok())
                common::Task::Halt("ai: camera wake wait failed\n");
        },
        [&] {
            ++state.loop_count;
            status = context.camera.Process();
            if (!status.Ok()) {
                status.LogStatus("camera");
                common::Task::Halt("ai: camera process failed\n");
            }
            camera_render::CameraRenderCycleContext cycle(context.camera.GetDiagnostics());
            cycle.did_work = (wake_reasons & (CameraRenderWake::kVsync | CameraRenderWake::kError)) != 0U;
            camera_render::HandleCameraRecoveryAndReportHealth(context, state, cycle);
            cycle.now = common::Task::Now();
            camera_render::ProcessShellTouchAndSyntheticFaceOutput(context, state, synthetic_face, cycle);
            camera_render::SubmitPipe2FramesForInferenceOrDisplay(context, state, cycle, wake_);
            camera_render::ReceiveInferenceResultsAndUpdateExposureControl(context, state, cycle, wake_);
            camera_render::ComposeAndPresentCameraFrameOrUiOverlay(context, state, cycle);
            camera_render::CountIdleWakeupsAndReportRenderStatistics(state, cycle);
        }
    );
}

} // namespace uai::ai::task
