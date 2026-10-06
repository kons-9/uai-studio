#include "task/camera_task.hpp"

#include <tk/tkernel.h>

#include "app_context.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "middleware/task/task.hpp"

namespace uai::ai::mini {

namespace {

/* Forward every completed Pipe2 frame to the inference task. The frame
 * channel returns dropped frames to the memory manager, so this never leaks
 * a DMA buffer even when inference is slower than the camera. */
void ForwardPipe2Frame(AppContext &app)
{
    pipeline::InferenceFrame frame{};
    const common::Error status = app.camera.TakeCompletedInference(&frame);
    if (status.Ok()) {
        if (app.external_nor_ready) {
            app.frames.Send(frame);
        } else {
            app.memory.ReleaseInferenceBuffer(frame).LogStatus("memory");
        }
        return;
    }
    if (status.Code() != common::ErrorCode::kNoFrame &&
        status.Code() != common::ErrorCode::kNoBuffer) {
        status.LogStatus("camera");
    }
}

} // namespace

void CameraTask::Start(middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_,
                        kCameraTaskPriority, "camera");
}

void CameraTask::Entry() { Instance().Run(); }

void CameraTask::Run()
{
    AppContext &app = App();

    common::Error status = app.lcd.ShowInitialFrame(inference::BoxSet{});
    if (!status.Ok()) {
        status.LogStatus("lcd");
        common::Task::Halt("mini: initial frame failed\n");
    }
    status = app.camera.Start();
    if (!status.Ok()) {
        status.LogStatus("camera");
        common::Task::Halt("mini: camera start failed\n");
    }

    inference::BoxSet shown_boxes{};
    std::uint32_t last_result_tick = common::Task::Now();
    std::uint32_t reported_recoveries = 0U;

    common::Task::RunForever(app.cpu_task_monitor, "camera",
                             [] { tk_dly_tsk(1); },
                             [&] {
        /* ISP updates and automatic recovery when frames stop arriving. */
        status = app.camera.Process();
        if (!status.Ok()) status.LogStatus("camera");
        const camera::Diagnostics diagnostics = app.camera.GetDiagnostics();
        if (diagnostics.recovery_count != reported_recoveries) {
            reported_recoveries = diagnostics.recovery_count;
            UAI_LOG_WARN("camera: recovery attempts=%u frames=%u pipe2=%u drops=%u\n",
                         reported_recoveries, diagnostics.frame_event_count,
                         diagnostics.pipe2_frame_event_count,
                         diagnostics.pipe2_drop_count);
        }

        ForwardPipe2Frame(app);

        /* Keep the previous boxes until a newer result arrives, but not
         * forever: a stalled model must not leave stale boxes on screen. */
        const std::uint32_t now = common::Task::Now();
        const message_channel::DrainResult drained =
            app.results.DrainLatest(&shown_boxes);
        if (drained.updated) {
            last_result_tick = now;
        } else if (!drained.error.Ok() &&
                   drained.error.Code() != common::ErrorCode::kNoFrame) {
            drained.error.LogStatus("results");
        }
        if (shown_boxes.person_valid && now - last_result_tick >= kResultHoldMs) {
            shown_boxes = inference::BoxSet{};
        }

        pipeline::CaptureFrame capture{};
        status = app.camera.TakeCompletedCapture(&capture);
        if (!status.Ok()) {
            if (status.Code() != common::ErrorCode::kNoFrame) {
                status.LogStatus("camera");
            }
            return;
        }
        status = app.lcd.ComposeAndPresent(capture, shown_boxes);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            if (!status.IsRoutine()) {
                common::Task::Halt("mini: lcd compose failed\n");
            }
        }
    });
}

} // namespace uai::ai::mini
