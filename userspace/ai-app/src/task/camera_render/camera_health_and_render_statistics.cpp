#include "task/camera_render/camera_render_context_and_operations.hpp"

#include "driver/cache_driver/cache_driver.hpp"
#include "driver/camera_driver/camera_diagnostics.hpp"
#include "driver/npu_driver/debug.h"
#include "memory_manager/memory_manager.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/pipeline/image_diagnostics.hpp"
#include "middleware/task/task.hpp"

namespace uai::ai::task::camera_render {

void HandleCameraRecoveryAndReportHealth(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle
)
{
    const auto &snapshot = cycle.diagnostics;
    if (state.observed_recovery_count != snapshot.recovery_count) {
        state.observed_recovery_count = snapshot.recovery_count;
        ResetInferenceResultsAndExposureControl(context, state, cycle);
    }
    auto &pipe_stats = state.schedule.Stats(RenderOperation::kPipe2);
    pipe_stats.dropped += snapshot.pipe2_drop_count - state.observed_pipe2_drops;
    state.observed_pipe2_drops = snapshot.pipe2_drop_count;
    if (state.diagnostics.ShouldReport(snapshot, [] {
            return common::Task::Now();
        })) {
        state.diagnostics.MarkReported(snapshot, common::Task::Now());
        UAI_LOG_WARN(
            "camera: health anomaly=%s tick=%u detail=%x timeout=%u/%u age_ms=%u/%u "
            "errors pipe=%u sensor=%u csi=%u isp=%u dcmipp=%x csi0=%x csi1=%x pend0=%x pend1=%x "
            "code=%x sot_sync_dl0=%u sot_sync_dl1=%u sot_dl0=%u sot_dl1=%u\n",
            camera::AnomalyName(snapshot.last_anomaly),
            snapshot.last_anomaly_tick,
            snapshot.last_anomaly_detail,
            snapshot.pipe1_timeout_count,
            snapshot.pipe2_timeout_count,
            snapshot.pipe1_frame_age_ms,
            snapshot.pipe2_frame_age_ms,
            snapshot.dcmipp_error_count,
            snapshot.camera_error_count,
            snapshot.csi_error_count,
            snapshot.isp_error_count,
            snapshot.dcmipp_last_status,
            snapshot.csi_last_status,
            snapshot.csi_last_status1,
            snapshot.csi_last_pending_status,
            snapshot.csi_last_pending_status1,
            snapshot.csi_last_error_code,
            snapshot.csi_sot_sync_dl0_count,
            snapshot.csi_sot_sync_dl1_count,
            snapshot.csi_sot_dl0_count,
            snapshot.csi_sot_dl1_count
        );
    }
    if (state.diagnostics.ShouldReportRecovery(snapshot)) {
        state.diagnostics.MarkRecoveryReported(snapshot);
        UAI_LOG_WARN(
            "camera: recovery attempts=%u failed=%u frames=%u vsync=%u pipe2=%u drops=%u\n",
            snapshot.recovery_count,
            snapshot.recovery_error_count,
            snapshot.frame_event_count,
            snapshot.vsync_event_count,
            snapshot.pipe2_frame_event_count,
            snapshot.pipe2_drop_count
        );
    }
}

void LogCaptureBrightnessAndSensorExposure(
    const CameraRenderContext &context,
    const pipeline::CaptureFrame &capture
)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    if ((capture.sequence % 30U) != 0U) {
        return;
    }

    const common::Error ownership_status = context.memory.ValidateCaptureFrame(capture);
    if (!ownership_status.Ok()) {
        ownership_status.LogStatus("camera-luminance");
        return;
    }
    const common::Error cache_status = context.cache.PrepareForCpuRead(capture.buffer);
    if (!cache_status.Ok()) {
        cache_status.LogStatus("camera-luminance");
        return;
    }

    const auto *pixels = reinterpret_cast<const std::uint16_t *>(capture.buffer.address);
    constexpr std::size_t kPixelCount = memory_manager::kCaptureBufferBytes / sizeof(*pixels);
    const auto luminance = pipeline::SampleRgb565Luminance(pixels, kPixelCount, 64U);

    const auto sensor = camera::ReadSensorDiagnostics();
    UAI_LOG_DEBUG(
        "camera: image brightness seq=%u mean=%u peak=%u exposure_us=%u exposure_lines=%u gain_mdB=%d "
        "regs=%d/%u,%u,%u\n",
        static_cast<unsigned int>(capture.sequence),
        static_cast<unsigned int>(luminance.mean),
        static_cast<unsigned int>(luminance.peak),
        sensor.exposure_us,
        sensor.exposure_lines,
        static_cast<int>(sensor.gain_mdB),
        static_cast<int>(sensor.register_status),
        static_cast<unsigned int>(sensor.vmax),
        static_cast<unsigned int>(sensor.shutter),
        static_cast<unsigned int>(sensor.gain)
    );
}

void LogRenderOperationScheduleStatistics(const CameraRenderState &state)
{
    constexpr const char *names[] = {"touch", "pipe2", "submit", "results", "exposure", "display"};
    for (std::size_t index = 0U; index < state.schedule.stats.size(); ++index) {
        const auto &sample = state.schedule.stats[index];
        UAI_LOG_INFO(
            "cpu: schedule name=%s due=%u started=%u completed=%u failed=%u skipped=%u dropped=%u "
            "late_ms=%u frame_ms=%u idle=%u drop_stride=%u drop_inactive=%u drop_source=%u drop_queue=%u\n",
            names[index],
            static_cast<unsigned int>(sample.due),
            static_cast<unsigned int>(sample.started),
            static_cast<unsigned int>(sample.completed),
            static_cast<unsigned int>(sample.failed),
            static_cast<unsigned int>(sample.skipped),
            static_cast<unsigned int>(sample.dropped),
            static_cast<unsigned int>(sample.max_lateness_ms),
            static_cast<unsigned int>(sample.max_frame_delay_ms),
            static_cast<unsigned int>(state.idle_wakeups),
            static_cast<unsigned int>(sample.dropped_stride),
            static_cast<unsigned int>(sample.dropped_inactive),
            static_cast<unsigned int>(sample.dropped_source),
            static_cast<unsigned int>(sample.dropped_queue)
        );
    }
}

void CountIdleWakeupsAndReportRenderStatistics(
    CameraRenderState &state,
    const CameraRenderCycleContext &cycle
)
{
    if (!cycle.did_work && !cycle.ui_dirty)
        ++state.idle_wakeups;
    if (state.schedule.ReportDue(common::Task::Now())) {
        LogRenderOperationScheduleStatistics(state);
        state.schedule.Reported(common::Task::Now());
    }
    if (cycle.features.frame_trace && cycle.capture_ready && (state.loop_count % 1000U) == 0U) {
        UAI_LOG_DEBUG(
            "camera: heartbeat loop=%u sequence=%u pipe2=%u drops=%u aton_irq=%u last=%x\n",
            static_cast<unsigned int>(state.loop_count),
            static_cast<unsigned int>(cycle.capture.sequence),
            cycle.diagnostics.pipe2_frame_event_count,
            cycle.diagnostics.pipe2_drop_count,
            g_aton_irq_count,
            g_aton_last_irqs
        );
    }
}

}