#include <cstdint>
#include <cstdio>
#include <cstring>

#include <tk/tkernel.h>

#include "driver/camera_driver/camera_driver.hpp"
#include "driver/camera_driver/camera_diagnostics.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/npu_driver/debug.h"
#include "driver/touch_driver/touch_driver.hpp"
#include "middleware/foundation/log.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/pipeline/image_diagnostics.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "middleware/ui/touch_point.hpp"
#include "task/camera_render_task.hpp"
#include "task/camera_render_state.hpp"
#include "middleware/task/task.hpp"
#include "task/task_context.hpp"
#include "ui/app_ui.hpp"
#include "ui/ui_layout.hpp"
#include "exposure_control/runtime.hpp"
#include "shell/owner.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task {

namespace {

struct RenderFeatures {
    bool input_display = false;
    bool inference_trace = false;
    bool frame_trace = false;
    bool brightness = false;
    bool display_trace = false;
    bool shows_camera = true;
    bool ai_exposure = false;
    std::uint8_t model_mask = 0U;
};

struct FrameCycleContext {
    explicit FrameCycleContext(const camera::Diagnostics &snapshot) : diagnostics(snapshot) {}
    FrameCycleContext(const FrameCycleContext &) = delete;
    FrameCycleContext &operator=(const FrameCycleContext &) = delete;

    const camera::Diagnostics diagnostics;
    std::uint32_t now = 0U;
    pipeline::InferenceFrame pipe2{};
    pipeline::CaptureFrame capture{};
    RenderFeatures features{};
};

void ReportSchedules(const CameraRenderState &state)
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

void InspectCaptureBrightness(
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

void InspectInferenceInput(
    const CameraRenderContext &context,
    const pipeline::InferenceFrame &frame
)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    if (!frame || !frame.from_pipe2 || frame.buffer.size < memory_manager::kInferenceFrameBytes) {
        UAI_LOG_TEXT(common::LogLevel::kDebug, "ai: input inspect invalid frame\n");
        return;
    }

    const buffer::Buffer input_buffer{
        frame.buffer.address, memory_manager::kInferenceFrameBytes, frame.buffer.index, buffer::Region::kInference
    };
    const common::Error cache_status = context.cache.PrepareForCpuRead(input_buffer);
    if (!cache_status.Ok()) {
        cache_status.LogStatus("ai-input");
        return;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(frame.buffer.address);
    const std::size_t byte_count = memory_manager::kInferenceFrameBytes;
    constexpr std::size_t kPixelCount = pipeline::kInferenceFormat.width * pipeline::kInferenceFormat.height;
    const auto statistics = pipeline::InspectRgb888(bytes, byte_count, kPixelCount, 64U);

    const std::size_t center = ((pipeline::kInferenceFormat.height / 2U) * pipeline::kInferenceFormat.width
                                + pipeline::kInferenceFormat.width / 2U)
        * 3U;
    UAI_LOG_DEBUG(
        "ai: input inspect seq=%u address=%x bytes=%u crc=%x "
        "min=%u max=%u mean_luma=%u p00=%02x/%02x/%02x "
        "pcenter=%02x/%02x/%02x plast=%02x/%02x/%02x\n",
        static_cast<unsigned int>(frame.capture_sequence),
        static_cast<unsigned int>(frame.buffer.address),
        static_cast<unsigned int>(byte_count),
        static_cast<unsigned int>(statistics.crc),
        static_cast<unsigned int>(statistics.minimum),
        static_cast<unsigned int>(statistics.maximum),
        static_cast<unsigned int>(statistics.mean_luminance),
        bytes[0U],
        bytes[1U],
        bytes[2U],
        bytes[center],
        bytes[center + 1U],
        bytes[center + 2U],
        bytes[byte_count - 3U],
        bytes[byte_count - 2U],
        bytes[byte_count - 1U]
    );
}

void DumpFrozenCapture(
    const CameraRenderContext &context,
    const pipeline::CaptureFrame &frame
)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    const auto camera_diagnostics = context.camera.GetDiagnostics();
    const auto *pixels = reinterpret_cast<const std::uint16_t *>(frame.buffer.address);
    const std::uint32_t full_crc =
        pipeline::Crc32Bytes(reinterpret_cast<const std::uint8_t *>(frame.buffer.address), frame.buffer.size);
    camera::DumpCaptureRegisters(frame, camera_diagnostics, full_crc);

    const auto rows = pipeline::InspectCaptureRows(pixels);
    UAI_LOG_DEBUG(
        "camera: frozen row distribution nonzero=%u zero=%u first=%u last=%u distinct_crc=%u\n",
        rows.nonzero_rows,
        rows.zero_rows,
        rows.nonzero_rows == 0U ? 0U : rows.first_nonzero,
        rows.nonzero_rows == 0U ? 0U : rows.last_nonzero,
        rows.distinct_row_crcs
    );
    std::uint32_t reported_nonzero = 0U;
    for (std::uint32_t y = 0U; y < pipeline::kCaptureFormat.height && reported_nonzero < 24U; ++y) {
        if (rows.has_data[y]) {
            UAI_LOG_DEBUG("camera: frozen nonzero row y=%u crc=%x\n", y, rows.row_crcs[y]);
            ++reported_nonzero;
        }
    }

    constexpr std::uint32_t kRows[] = {0U, 1U, 35U, 194U, 240U, 400U, 479U};
    constexpr std::uint32_t kColumns[] = {0U, 1U, 16U, 39U, 40U, 799U};
    for (const std::uint32_t y : kRows) {
        UAI_LOG_DEBUG("camera: frozen row y=%u crc=%x samples=", y, rows.row_crcs[y]);
        for (const std::uint32_t x : kColumns) {
            UAI_LOG_DEBUG(
                "%04x%s",
                static_cast<unsigned int>(pixels[y * pipeline::kCaptureFormat.width + x]),
                x == kColumns[sizeof(kColumns) / sizeof(kColumns[0]) - 1U] ? "\n" : ","
            );
        }
    }
}

} // namespace

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

    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: driver ready\n");
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: initial frame begin\n");
    status = context.lcd.ShowInitialFrame(initial, kDisplayCoordinatePatternDiagnostic);
    if (!status.Ok()) {
        status.LogStatus("lcd");
        common::Task::Halt("ai: initial frame failed\n");
    }
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: initial buffer ready box_count=0\n");

    if constexpr (kDisplayCoordinatePatternDiagnostic) {
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: diagnostic frame did not latch\n");
        }
        UAI_LOG_DEBUG(
            "lcd: diagnostic readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n",
            static_cast<unsigned int>(LTDC_Layer1->CFBAR),
            static_cast<unsigned int>(LTDC_Layer1->CR),
            static_cast<unsigned int>(LTDC_Layer1->PFCR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
            static_cast<unsigned int>(LTDC->AWCR),
            static_cast<unsigned int>(LTDC->TWCR),
            static_cast<unsigned int>(LTDC->ISR)
        );
        UAI_LOG_TEXT(
            uai::ai::common::LogLevel::kDebug, "lcd: static coordinate pattern active; camera remains stopped\n"
        );
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    if constexpr (kSyntheticComposeDiagnostic) {
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: initial diagnostic frame did not latch\n");
        }

        std::uintptr_t capture0 = 0U;
        std::uintptr_t capture1 = 0U;
        status = context.memory.CaptureBuffers(&capture0, &capture1);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic capture buffers unavailable\n");
        }
        const uai::ai::buffer::Buffer source_buffer{
            capture0, uai::ai::memory_manager::kCaptureBufferBytes, 0U, uai::ai::buffer::Region::kCapture
        };
        status = context.cache.PrepareForDmaWrite(source_buffer);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic source cache prepare failed\n");
        }
        status = context.lcd.GenerateCoordinatePattern(source_buffer);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: diagnostic pattern generation failed\n");
        }
        SCB_CleanDCache_by_Addr(
            reinterpret_cast<std::uint32_t *>(source_buffer.address), static_cast<std::int32_t>(source_buffer.size)
        );

        uai::ai::pipeline::CaptureFrame synthetic_capture{};
        status = context.memory.ImportCompletedCapture(source_buffer.address, &synthetic_capture);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic capture import failed\n");
        }
        UAI_LOG_DEBUG(
            "lcd: synthetic compose begin sequence=%u source=%x bytes=%u\n",
            static_cast<unsigned int>(synthetic_capture.sequence),
            static_cast<unsigned int>(source_buffer.address),
            static_cast<unsigned int>(source_buffer.size)
        );
        status = context.lcd.ComposeAndPresent(synthetic_capture, initial, nullptr, true);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: synthetic compose failed\n");
        }
        UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: synthetic compose presented; camera remains stopped\n");
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: driver ready\n");
    status = context.camera.Start();
    if (!status.Ok()) {
        status.LogStatus("camera");
        common::Task::Halt("ai: camera start failed\n");
    }
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: start result=ok detail=0\n");
    if constexpr (kLiveCaptureFreezeDiagnostic) {
        uai::ai::pipeline::CaptureFrame first_capture{};
        const std::uint32_t wait_start = common::Task::Now();
        for (;;) {
            status = context.camera.Process();
            if (!status.Ok()) {
                status.LogStatus("camera");
                common::Task::Halt("ai: frozen capture process failed\n");
            }
            uai::ai::pipeline::CaptureFrame candidate{};
            status = context.camera.TakeCompletedCapture(&candidate);
            if (status.Ok()) {
                if ((candidate.sequence % 10U) == 0U) {
                    InspectCaptureBrightness(context, candidate);
                    UAI_LOG_DEBUG(
                        "camera: freeze warmup sequence=%u events=%u buffer=%x\n",
                        static_cast<unsigned int>(candidate.sequence),
                        context.camera.GetDiagnostics().frame_event_count,
                        static_cast<unsigned int>(candidate.buffer.address)
                    );
                }
                if (candidate.sequence >= 30U) {
                    first_capture = candidate;
                    break;
                }
                continue;
            }
            if (status.Code() != common::ErrorCode::kNoFrame) {
                status.LogStatus("camera");
                common::Task::Halt("ai: frozen capture acquire failed\n");
            }
            if (static_cast<std::uint32_t>(common::Task::Now() - wait_start) >= 3000U) {
                common::Task::Halt("ai: frozen capture timed out\n");
            }
            tk_dly_tsk(1);
        }
        const unsigned int events_before_stop = context.camera.GetDiagnostics().frame_event_count;
        UAI_LOG_DEBUG(
            "camera: freeze requested sequence=%u address=%x events=%u\n",
            static_cast<unsigned int>(first_capture.sequence),
            static_cast<unsigned int>(first_capture.buffer.address),
            events_before_stop
        );
        status = context.camera.Stop();
        if (!status.Ok()) {
            status.LogStatus("camera");
            common::Task::Halt("ai: camera stop failed; capture not inspected\n");
        }

        uai::ai::pipeline::CaptureFrame latest_capture{};
        const common::Error latest_status = context.camera.TakeCompletedCapture(&latest_capture);
        if (latest_status.Ok()) {
            first_capture = latest_capture;
        } else if (latest_status.Code() != common::ErrorCode::kNoFrame) {
            latest_status.LogStatus("camera");
            common::Task::Halt("ai: stopped capture acquire failed\n");
        }
        status = context.cache.PrepareForCpuRead(first_capture.buffer);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: frozen capture cache invalidate failed\n");
        }
        UAI_LOG_DEBUG(
            "camera: freeze complete events_before=%u events_after=%u selected_sequence=%u selected_address=%x\n",
            events_before_stop,
            context.camera.GetDiagnostics().frame_event_count,
            static_cast<unsigned int>(first_capture.sequence),
            static_cast<unsigned int>(first_capture.buffer.address)
        );
        DumpFrozenCapture(context, first_capture);

        status = context.lcd.ComposeAndPresent(first_capture, initial, nullptr, true);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: frozen live capture compose failed\n");
        }
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: frozen live capture display did not latch\n");
        }
        UAI_LOG_DEBUG(
            "lcd: frozen capture readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n",
            static_cast<unsigned int>(LTDC_Layer1->CFBAR),
            static_cast<unsigned int>(LTDC_Layer1->CR),
            static_cast<unsigned int>(LTDC_Layer1->PFCR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLR),
            static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
            static_cast<unsigned int>(LTDC->AWCR),
            static_cast<unsigned int>(LTDC->TWCR),
            static_cast<unsigned int>(LTDC->ISR)
        );
        UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: frozen raw capture displayed; camera stopped\n");
        for (;;) {
            tk_dly_tsk(1000);
        }
    }
    UAI_LOG_INFO(
        "camera: irq dcmipp=%u/%u csi=%u/%u\n",
        static_cast<unsigned int>(NVIC_GetEnableIRQ(DCMIPP_IRQn)),
        static_cast<unsigned int>(NVIC_GetPendingIRQ(DCMIPP_IRQn)),
        static_cast<unsigned int>(NVIC_GetEnableIRQ(CSI_IRQn)),
        static_cast<unsigned int>(NVIC_GetPendingIRQ(CSI_IRQn))
    );

    CameraRenderState state(context.pipeline_task, initial, common::Task::Now());
    auto &active_boxes = state.results.boxes;
    auto &exposure = state.exposure;
    auto &exposure_mode = state.exposure_mode;
    auto &screen_ui = state.ui;
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
            FrameCycleContext cycle(context.camera.GetDiagnostics());
            const auto &snapshot = cycle.diagnostics;
            bool did_work = (wake_reasons & (CameraRenderWake::kVsync | CameraRenderWake::kError)) != 0U;
            bool ui_dirty = false;
            const auto reset_results = [&] {
                state.results.Reset(context.pipeline_task.AdvanceGeneration());
                exposure.Reset();
                state.schedule.ConfigureFrames(
                    kInferenceFrameStride,
                    context.camera.GetDiagnostics().pipe2_latest_capture_sequence + kInferenceFrameStride
                );
                ui_dirty = true;
                did_work = true;
            };
            if (state.observed_recovery_count != snapshot.recovery_count) {
                state.observed_recovery_count = snapshot.recovery_count;
                reset_results();
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

            cycle.now = common::Task::Now();
            const std::uint32_t now = cycle.now;
            shell::Request shell_request{};
            if (context.shell_mailbox.Take(&shell_request)) {
                did_work = true;
                ui_dirty = true;
                shell::Reply reply{};
                if (shell_request.action == shell::Action::kDiagnostics) {
                    std::atomic<bool> *option = nullptr;
                    constexpr const char *names[] = {
                        "frame", "brightness", "input", "input_display", "inference", "fps", "display", "timing"
                    };
                    switch (shell_request.values[0]) {
                    case 0:
                        option = &context.diagnostics.camera_frame_trace;
                        break;
                    case 1:
                        option = &context.diagnostics.camera_brightness;
                        break;
                    case 2:
                        option = &context.diagnostics.inference_input;
                        break;
                    case 3:
                        option = &context.diagnostics.inference_input_display;
                        break;
                    case 4:
                        option = &context.diagnostics.inference_trace;
                        break;
                    case 5:
                        option = &context.diagnostics.inference_fps;
                        break;
                    case 6:
                        option = &context.diagnostics.display_trace;
                        break;
                    case 7:
                        option = &context.diagnostics.display_timing;
                        break;
                    default:
                        break;
                    }
                    if (option == nullptr) {
                        reply.code = static_cast<std::int32_t>(common::ErrorCode::kInvalidArgument);
                    } else {
                        if (shell_request.values[1] != -1) {
                            option->store(shell_request.values[1] != 0, std::memory_order_relaxed);
                            if (shell_request.values[0] == 7)
                                context.lcd.SetTimingDiagnostics(option->load(std::memory_order_relaxed));
                        }
                        std::snprintf(
                            reply.text,
                            sizeof(reply.text),
                            "diag %s=%s\r\n",
                            names[shell_request.values[0]],
                            option->load(std::memory_order_relaxed) ? "on" : "off"
                        );
                    }
                } else {
                    reply = shell::Apply(shell_request, context.camera, screen_ui, exposure_mode);
                }
                context.shell_mailbox.Complete(reply);
                if (shell_request.action == shell::Action::kCameraFps
                    || shell_request.action == shell::Action::kCameraFlip
                    || shell_request.action == shell::Action::kCameraCrop) {
                    camera::Geometry geometry{};
                    const auto geometry_status = context.camera.GetGeometry(&geometry);
                    if (geometry_status.Ok()
                        && (geometry.fps != state.geometry.fps || geometry.horizontal != state.geometry.horizontal
                            || geometry.vertical != state.geometry.vertical || geometry.crop.x != state.geometry.crop.x
                            || geometry.crop.y != state.geometry.crop.y
                            || geometry.crop.width != state.geometry.crop.width
                            || geometry.crop.height != state.geometry.crop.height)) {
                        state.geometry = geometry;
                        reset_results();
                    }
                }
            }
            if (context.touch_ready && state.schedule.TouchDue(now)) {
                state.schedule.TouchPolled(now);
                did_work = true;
                ui::TouchPoint sample{};
                const common::Error touch_status = context.touch.Read(&sample);
                state.schedule.Stats(RenderOperation::kTouch).Finish(touch_status.Ok());
                if (!touch_status.Ok()) {
                    touch_status.LogStatus("touch");
                } else {
                    const ui::Event event = screen_ui.HandleTouch(sample);
                    ui_dirty = ui_dirty || event.type != ui::EventType::kNone;
                    shell::Reply touch_reply{};
                    if (shell::ApplyTouch(event, context.camera, screen_ui, exposure_mode, &touch_reply)
                        && touch_reply.code != 0)
                        UAI_LOG_WARN("ui: exposure apply failed code=%d\n", static_cast<int>(touch_reply.code));
                    if (event.type == ui::EventType::kPress && context.diagnostics.display_trace) {
                        UAI_LOG_DEBUG(
                            "ui: press id=%u x=%u y=%u\n",
                            static_cast<unsigned int>(event.widget_id),
                            static_cast<unsigned int>(event.x),
                            static_cast<unsigned int>(event.y)
                        );
                    }
                }
            }
            cycle.features = {
                context.diagnostics.inference_input_display.load(std::memory_order_relaxed),
                context.diagnostics.inference_trace.load(std::memory_order_relaxed),
                context.diagnostics.camera_frame_trace.load(std::memory_order_relaxed),
                context.diagnostics.camera_brightness.load(std::memory_order_relaxed),
                context.diagnostics.display_trace.load(std::memory_order_relaxed),
                screen_ui.ShowsCamera(),
                screen_ui.AiExposureEnabled(),
                screen_ui.ModelMask()
            };
            const auto &features = cycle.features;
            const bool inference_enabled = kCopyInferenceFrames
                && (kInferenceMode == InferenceMode::kCopyOnly || context.external_nor_ready)
                && !features.input_display;
            const auto release_inference = [&](const pipeline::InferenceFrame &frame) {
                const auto released = context.memory.ReleaseInferenceBuffer(frame);
                if (!released.Ok()) {
                    released.LogStatus("memory");
                    common::Task::Halt("ai: inference lease release failed\n");
                }
                return released;
            };
            auto &pipe2_frame = cycle.pipe2;
            for (std::size_t drained = 0U; drained < memory_manager::kInferenceBufferCount; ++drained) {
                pipe2_frame = {};
                const auto pipe2_status = context.camera.TakeCompletedInference(&pipe2_frame);
                if (!pipe2_status.Ok()) {
                    if (pipe2_status.Code() != common::ErrorCode::kNoFrame
                        && pipe2_status.Code() != common::ErrorCode::kNoBuffer)
                        pipe2_status.LogStatus("camera");
                    break;
                }
                did_work = true;
                state.schedule.StartEvent(RenderOperation::kPipe2);
                if (features.input_display) {
                    auto &submit = state.schedule.Stats(RenderOperation::kSubmit);
                    ++submit.dropped;
                    ++submit.dropped_inactive;
                    status = context.memory.ClaimInferenceBuffer(pipe2_frame);
                    if (!status.Ok()) {
                        status.LogStatus("memory");
                        common::Task::Halt("ai: inference input display claim failed\n");
                    }
                    const bool log_input = context.diagnostics.inference_input
                        && (pipe2_frame.capture_sequence <= 3U || (pipe2_frame.capture_sequence % 30U) == 0U);
                    if (log_input) {
                        UAI_LOG_DEBUG(
                            "ai: input display live sequence=%u buffer=%x\n",
                            static_cast<unsigned int>(pipe2_frame.capture_sequence),
                            static_cast<unsigned int>(pipe2_frame.buffer.address)
                        );
                        InspectInferenceInput(context, pipe2_frame);
                    }
                    status = context.lcd.ComposeInferenceAndPresent(pipe2_frame);
                    if (!status.Ok() && (!status.IsRoutine() || log_input)) {
                        status.LogStatus("lcd");
                    }
                    (void)release_inference(pipe2_frame);
                } else if (inference_enabled
                           && state.schedule.TakeInference(pipe2_frame.capture_sequence, state.geometry.fps)) {
                    auto &submit = state.schedule.Stats(RenderOperation::kSubmit);
                    ++submit.started;
                    if (kInferenceMode == InferenceMode::kNpu) {
                        status = context.camera.SnapshotInferenceSource(&pipe2_frame);
                        if (!status.Ok()) {
                            status.LogStatus("camera");
                            (void)release_inference(pipe2_frame);
                            ++submit.dropped;
                            ++submit.dropped_source;
                        } else {
                            status = context.pipeline_task.InferenceFrames().Send(
                                pipe2_frame, context.pipeline_task.Generation()
                            );
                        }
                    } else {
                        status = release_inference(pipe2_frame);
                    }
                    submit.Finish(status.Ok());
                } else {
                    status = release_inference(pipe2_frame);
                    auto &submit = state.schedule.Stats(RenderOperation::kSubmit);
                    ++submit.dropped;
                    if (inference_enabled)
                        ++submit.dropped_stride;
                    else
                        ++submit.dropped_inactive;
                }
                pipe_stats.Finish(status.Ok());
                if (drained + 1U == memory_manager::kInferenceBufferCount)
                    wake_.Notify(CameraRenderWake::kPipe2);
            }
            const auto queue_drops = context.pipeline_task.InferenceFrames().Dropped();
            state.schedule.Stats(RenderOperation::kSubmit).dropped += queue_drops - state.observed_queue_drops;
            state.schedule.Stats(RenderOperation::kSubmit).dropped_queue = queue_drops;
            state.observed_queue_drops = queue_drops;
            ModelResultSnapshot incoming{};
            const message_channel::DrainResult drain_result = context.pipeline_task.TryGetLatestResult(&incoming);
            if (drain_result.updated) {
                state.schedule.StartEvent(RenderOperation::kResults);
                auto &results = state.schedule.Stats(RenderOperation::kResults);
                if (state.results.Accept(incoming))
                    results.Finish(true);
                else
                    ++results.skipped;
                did_work = true;
            }
            if (drain_result.error.Code() == common::ErrorCode::kTimeout)
                wake_.Notify(CameraRenderWake::kResult);
            ui_dirty = state.results.Expire(now) || ui_dirty;
            if constexpr (kAiExposureControl) {
                const auto exposure_results =
                    context.pipeline_task.ConsumeExposureResults([&](const inference::BoxSet &result) {
                        if (features.ai_exposure && state.previous_exposure_enabled)
                            exposure.Observe(result, now);
                    });
                if (exposure_results.error.Code() == common::ErrorCode::kTimeout)
                    wake_.Notify(CameraRenderWake::kResult);
                if (!exposure_results.error.Ok() && exposure_results.error.Code() != common::ErrorCode::kNoFrame
                    && exposure_results.error.Code() != common::ErrorCode::kTimeout)
                    exposure_results.error.LogStatus("exposure-results");
                state.previous_exposure_enabled = features.ai_exposure;
                if (exposure.RemainingWait(now, features.ai_exposure) == 0U) {
                    state.schedule.StartEvent(RenderOperation::kExposure);
                    const auto exposure_status = exposure.Process(
                        context.camera,
                        now,
                        features.ai_exposure,
                        !exposure_mode.manual,
                        !exposure_mode.custom_statistics
                    );
                    state.schedule.Stats(RenderOperation::kExposure).Finish(exposure_status.Ok());
                    did_work = true;
                    if (!exposure_status.Ok()
                        && (state.last_exposure_error_tick == 0U || now - state.last_exposure_error_tick >= 1000U)) {
                        exposure_status.LogStatus("exposure");
                        state.last_exposure_error_tick = now;
                    }
                }
            }
            ui_dirty =
                screen_ui.UpdateStatus(now, kAiExposureControl ? &exposure.DisplayValues() : nullptr) || ui_dirty;
            if (!drain_result.error.Ok() && drain_result.error.Code() != common::ErrorCode::kNoFrame) {
                drain_result.error.LogStatus("results");
            }
            if (drain_result.updated) {
                if (context.diagnostics.display_trace) {
                    UAI_LOG_DEBUG(
                        "lcd: box source=ai sequence=%u capture=%u count=%u\n",
                        static_cast<unsigned int>(active_boxes.model_sequence),
                        static_cast<unsigned int>(active_boxes.capture_sequence),
                        static_cast<unsigned int>(active_boxes.person.count + active_boxes.face.count)
                    );
                    UAI_LOG_DEBUG(
                        "lcd: boxes person=%u face=%u mask_px=%u\n",
                        static_cast<unsigned int>(active_boxes.person.count),
                        static_cast<unsigned int>(active_boxes.face.count),
                        static_cast<unsigned int>(active_boxes.segmentation.mask_foreground_pixels)
                    );
                }
                /* The latest inference result is authoritative. Until it arrives,
             * keep presenting the previous result so model switching does not
             * create a blank frame. */
            }
            auto &capture = cycle.capture;
            status = context.camera.TakeCompletedCapture(&capture);
            const bool capture_ready = status.Ok();
            if (!status.Ok()) {
                if (status.Code() != common::ErrorCode::kNoFrame) {
                    status.LogStatus("camera");
                }
            }

            if (capture_ready && features.frame_trace) {
                if (capture.sequence <= 3U || (capture.sequence % 10U) == 0U) {
                    UAI_LOG_DEBUG(
                        "camera: frame captured sequence=%u buffer=%x event=%u aton_irq=%u last=%x\n",
                        static_cast<unsigned int>(capture.sequence),
                        static_cast<unsigned int>(capture.buffer.address),
                        snapshot.frame_event_count,
                        g_aton_irq_count,
                        g_aton_last_irqs
                    );
                }
            }
            if (capture_ready && features.brightness) {
                InspectCaptureBrightness(context, capture);
            }

            /* In the live Pipe2 diagnostic mode, the LCD is reserved for the
         * actual inference input. Capture buffers are still drained below. */
            const bool display_due = state.ShouldPresent(capture_ready, ui_dirty, features.input_display);
            if (display_due) {
                state.schedule.StartEvent(RenderOperation::kDisplay);
                did_work = true;
                if (context.diagnostics.display_trace && (capture.sequence <= 3U || (capture.sequence % 10U) == 0U)) {
                    UAI_LOG_DEBUG("lcd: compose begin sequence=%u\n", static_cast<unsigned int>(capture.sequence));
                }
                /* Screens without the camera repaint from scratch; the capture
             * is still taken above so its buffer returns to the pool. */
                status = features.shows_camera
                    ? context.lcd.ComposeAndPresent(capture, screen_ui.VisibleBoxes(active_boxes), &screen_ui.Overlay())
                    : context.lcd.PresentOverlay(screen_ui.Overlay());
                auto &display = state.schedule.Stats(RenderOperation::kDisplay);
                display.Finish(status.Ok());
                if (status.Ok() && capture_ready) {
                    const auto delay = HAL_GetTick() - capture.completed_ms;
                    if (delay > display.max_frame_delay_ms)
                        display.max_frame_delay_ms = delay;
                }
                if (!status.Ok()) {
                    status.LogStatus("lcd");
                    if (!status.IsRoutine()) {
                        common::Task::Halt("ai: lcd compose failed\n");
                    }
                } else if (context.diagnostics.display_trace
                           && (capture.sequence <= 3U || (capture.sequence % 10U) == 0U)) {
                    UAI_LOG_DEBUG(
                        "lcd: frame presented sequence=%u buffer=%x\n",
                        static_cast<unsigned int>(capture.sequence),
                        static_cast<unsigned int>(capture.buffer.address)
                    );
                }
                if (context.diagnostics.display_trace && (capture.sequence <= 3U || (capture.sequence % 10U) == 0U)) {
                    UAI_LOG_DEBUG(
                        "lcd: compose end sequence=%u aton_irq=%u last=%x\n",
                        static_cast<unsigned int>(capture.sequence),
                        g_aton_irq_count,
                        g_aton_last_irqs
                    );
                }
            }

            if (!did_work && !ui_dirty)
                ++state.idle_wakeups;
            if (state.schedule.ReportDue(common::Task::Now())) {
                ReportSchedules(state);
                state.schedule.Reported(common::Task::Now());
            }
            if (features.frame_trace && capture_ready && (state.loop_count % 1000U) == 0U) {
                UAI_LOG_DEBUG(
                    "camera: heartbeat loop=%u sequence=%u pipe2=%u drops=%u aton_irq=%u last=%x\n",
                    static_cast<unsigned int>(state.loop_count),
                    static_cast<unsigned int>(capture.sequence),
                    snapshot.pipe2_frame_event_count,
                    snapshot.pipe2_drop_count,
                    g_aton_irq_count,
                    g_aton_last_irqs
                );
            }
        }
    );
}

} // namespace uai::ai::task
