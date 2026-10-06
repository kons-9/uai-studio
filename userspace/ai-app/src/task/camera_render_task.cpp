#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

#include "driver/camera_driver/camera_driver.hpp"
#include "driver/camera_driver/camera_diagnostics.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/npu_driver/debug.h"
#include "middleware/foundation/log.hpp"
#include "memory_manager/memory_sizes.hpp"
#include "middleware/ai_runtime/inference_result_types.hpp"
#include "middleware/pipeline/image_diagnostics.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "task/camera_render_task.hpp"
#include "middleware/task/task.hpp"
#include "task/task_context.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task {

namespace {

void InspectCaptureBrightness(const CameraRenderContext &context,
                              const pipeline::CaptureFrame &capture)
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
    const common::Error cache_status =
        context.cache.PrepareForCpuRead(capture.buffer);
    if (!cache_status.Ok()) {
        cache_status.LogStatus("camera-luminance");
        return;
    }

    const auto *pixels =
        reinterpret_cast<const std::uint16_t *>(capture.buffer.address);
    constexpr std::size_t kPixelCount =
        memory_manager::kCaptureBufferBytes / sizeof(*pixels);
    const auto luminance = pipeline::SampleRgb565Luminance(
        pixels, kPixelCount, 64U);

    const auto sensor = camera::ReadSensorDiagnostics();
    UAI_LOG_DEBUG("camera: image brightness seq=%u mean=%u peak=%u exposure_us=%u exposure_lines=%u gain_mdB=%d regs=%d/%u,%u,%u\n",
              static_cast<unsigned int>(capture.sequence),
              static_cast<unsigned int>(luminance.mean),
              static_cast<unsigned int>(luminance.peak),
              sensor.exposure_us,
              sensor.exposure_lines,
              static_cast<int>(sensor.gain_mdB),
              static_cast<int>(sensor.register_status),
              static_cast<unsigned int>(sensor.vmax),
              static_cast<unsigned int>(sensor.shutter),
              static_cast<unsigned int>(sensor.gain));
}

void InspectInferenceInput(const CameraRenderContext &context,
                           const pipeline::InferenceFrame &frame)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    if (!frame || !frame.from_pipe2 ||
        frame.buffer.size < memory_manager::kInferenceFrameBytes) {
        UAI_LOG_TEXT(common::LogLevel::kDebug, "ai: input inspect invalid frame\n");
        return;
    }

    const buffer::Buffer input_buffer{
        frame.buffer.address,
        memory_manager::kInferenceFrameBytes,
        frame.buffer.index, buffer::Region::kInference};
    const common::Error cache_status =
        context.cache.PrepareForCpuRead(input_buffer);
    if (!cache_status.Ok()) {
        cache_status.LogStatus("ai-input");
        return;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(
        frame.buffer.address);
    const std::size_t byte_count = memory_manager::kInferenceFrameBytes;
    constexpr std::size_t kPixelCount =
        pipeline::kInferenceFormat.width * pipeline::kInferenceFormat.height;
    const auto statistics = pipeline::InspectRgb888(
        bytes, byte_count, kPixelCount, 64U);

    const std::size_t center =
        ((pipeline::kInferenceFormat.height / 2U) *
             pipeline::kInferenceFormat.width +
         pipeline::kInferenceFormat.width / 2U) * 3U;
    UAI_LOG_DEBUG("ai: input inspect seq=%u address=%x bytes=%u crc=%x "
                  "min=%u max=%u mean_luma=%u p00=%02x/%02x/%02x "
                  "pcenter=%02x/%02x/%02x plast=%02x/%02x/%02x\n",
              static_cast<unsigned int>(frame.capture_sequence),
              static_cast<unsigned int>(frame.buffer.address),
              static_cast<unsigned int>(byte_count),
              static_cast<unsigned int>(statistics.crc),
              static_cast<unsigned int>(statistics.minimum),
              static_cast<unsigned int>(statistics.maximum),
              static_cast<unsigned int>(statistics.mean_luminance),
              bytes[0U], bytes[1U], bytes[2U], bytes[center],
              bytes[center + 1U], bytes[center + 2U],
              bytes[byte_count - 3U], bytes[byte_count - 2U],
              bytes[byte_count - 1U]);
}

void DumpFrozenCapture(const CameraRenderContext &context,
                       const pipeline::CaptureFrame &frame)
{
    if (!common::IsLogEnabled(common::LogLevel::kDebug)) {
        return;
    }
    const auto camera_diagnostics = context.camera.GetDiagnostics();
    const auto *pixels = reinterpret_cast<const std::uint16_t *>(
        frame.buffer.address);
    const std::uint32_t full_crc = pipeline::Crc32Bytes(
        reinterpret_cast<const std::uint8_t *>(frame.buffer.address),
        frame.buffer.size);
    camera::DumpCaptureRegisters(frame, camera_diagnostics, full_crc);

    const auto rows = pipeline::InspectCaptureRows(pixels);
    UAI_LOG_DEBUG("camera: frozen row distribution nonzero=%u zero=%u first=%u last=%u distinct_crc=%u\n",
              rows.nonzero_rows, rows.zero_rows,
              rows.nonzero_rows == 0U ? 0U : rows.first_nonzero,
              rows.nonzero_rows == 0U ? 0U : rows.last_nonzero,
              rows.distinct_row_crcs);
    std::uint32_t reported_nonzero = 0U;
    for (std::uint32_t y = 0U;
         y < pipeline::kCaptureFormat.height &&
         reported_nonzero < 24U;
         ++y) {
        if (rows.has_data[y]) {
            UAI_LOG_DEBUG("camera: frozen nonzero row y=%u crc=%x\n",
                      y, rows.row_crcs[y]);
            ++reported_nonzero;
        }
    }

    constexpr std::uint32_t kRows[] = {0U, 1U, 35U, 194U, 240U, 400U, 479U};
    constexpr std::uint32_t kColumns[] = {0U, 1U, 16U, 39U, 40U, 799U};
    for (const std::uint32_t y : kRows) {
        UAI_LOG_DEBUG("camera: frozen row y=%u crc=%x samples=",
                  y, rows.row_crcs[y]);
        for (const std::uint32_t x : kColumns) {
            UAI_LOG_DEBUG("%04x%s",
                      static_cast<unsigned int>(
                          pixels[y * pipeline::kCaptureFormat.width + x]),
                      x == kColumns[sizeof(kColumns) / sizeof(kColumns[0]) - 1U]
                          ? "\n"
                          : ",");
        }
    }
}

} // namespace

void CameraRenderTask::Entry()
{
    TaskContext &root = GetTaskContext();
    root.camera_task.Run(root.CameraContext());
}

void CameraRenderTask::Start(
    middleware::cpu_task_monitor::CpuTaskMonitor &monitor)
{
    common::Task::Start(monitor, reinterpret_cast<FP>(Entry), stack_,
                5, "camera_render");
}

void CameraRenderTask::Run(CameraRenderContext context)
{
    common::Error status{};

    const inference::BoxSet initial{};

    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: driver ready\n");
    UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: initial frame begin\n");
    status = context.lcd.ShowInitialFrame(initial,
                                  kDisplayCoordinatePatternDiagnostic);
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
        UAI_LOG_DEBUG("lcd: diagnostic readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n",
                  static_cast<unsigned int>(LTDC_Layer1->CFBAR),
                  static_cast<unsigned int>(LTDC_Layer1->CR),
                  static_cast<unsigned int>(LTDC_Layer1->PFCR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
                  static_cast<unsigned int>(LTDC->AWCR),
                  static_cast<unsigned int>(LTDC->TWCR),
                  static_cast<unsigned int>(LTDC->ISR));
        UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "lcd: static coordinate pattern active; camera remains stopped\n");
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
            capture0, uai::ai::memory_manager::kCaptureBufferBytes, 0U,
            uai::ai::buffer::Region::kCapture};
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
            reinterpret_cast<std::uint32_t *>(source_buffer.address),
            static_cast<std::int32_t>(source_buffer.size));

        uai::ai::pipeline::CaptureFrame synthetic_capture{};
        status = context.memory.ImportCompletedCapture(source_buffer.address,
                                                        &synthetic_capture);
        if (!status.Ok()) {
            status.LogStatus("memory");
            common::Task::Halt("ai: diagnostic capture import failed\n");
        }
        UAI_LOG_DEBUG("lcd: synthetic compose begin sequence=%u source=%x bytes=%u\n",
                  static_cast<unsigned int>(synthetic_capture.sequence),
                  static_cast<unsigned int>(source_buffer.address),
                  static_cast<unsigned int>(source_buffer.size));
        status = context.lcd.ComposeAndPresent(synthetic_capture, initial, true);
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
                    UAI_LOG_DEBUG("camera: freeze warmup sequence=%u events=%u buffer=%x\n",
                              static_cast<unsigned int>(candidate.sequence),
                              context.camera.GetDiagnostics().frame_event_count,
                              static_cast<unsigned int>(candidate.buffer.address));
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
        const unsigned int events_before_stop =
            context.camera.GetDiagnostics().frame_event_count;
        UAI_LOG_DEBUG("camera: freeze requested sequence=%u address=%x events=%u\n",
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address),
                  events_before_stop);
        status = context.camera.Stop();
        if (!status.Ok()) {
            status.LogStatus("camera");
            common::Task::Halt("ai: camera stop failed; capture not inspected\n");
        }

        uai::ai::pipeline::CaptureFrame latest_capture{};
        const common::Error latest_status =
            context.camera.TakeCompletedCapture(&latest_capture);
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
        UAI_LOG_DEBUG("camera: freeze complete events_before=%u events_after=%u selected_sequence=%u selected_address=%x\n",
                  events_before_stop,
                  context.camera.GetDiagnostics().frame_event_count,
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address));
        DumpFrozenCapture(context, first_capture);

        status = context.lcd.ComposeAndPresent(first_capture, initial, true);
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: frozen live capture compose failed\n");
        }
        status = context.lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            status.LogStatus("lcd");
            common::Task::Halt("ai: frozen live capture display did not latch\n");
        }
        UAI_LOG_DEBUG("lcd: frozen capture readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n",
                  static_cast<unsigned int>(LTDC_Layer1->CFBAR),
                  static_cast<unsigned int>(LTDC_Layer1->CR),
                  static_cast<unsigned int>(LTDC_Layer1->PFCR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
                  static_cast<unsigned int>(LTDC->AWCR),
                  static_cast<unsigned int>(LTDC->TWCR),
                  static_cast<unsigned int>(LTDC->ISR));
        UAI_LOG_TEXT(uai::ai::common::LogLevel::kDebug, "camera: frozen raw capture displayed; camera stopped\n");
        for (;;) {
            tk_dly_tsk(1000);
        }
    }
    UAI_LOG_INFO("camera: irq dcmipp=%u/%u csi=%u/%u\n",
                 static_cast<unsigned int>(NVIC_GetEnableIRQ(DCMIPP_IRQn)),
                 static_cast<unsigned int>(NVIC_GetPendingIRQ(DCMIPP_IRQn)),
                 static_cast<unsigned int>(NVIC_GetEnableIRQ(CSI_IRQn)),
                 static_cast<unsigned int>(NVIC_GetPendingIRQ(CSI_IRQn)));

    inference::BoxSet active_boxes = initial;
    std::uint32_t next_inference = common::Task::Now() + kInferencePeriod;

    std::uint32_t loop_count = 0U;
    unsigned int reported_pipe_errors = 0U;
    unsigned int reported_camera_errors = 0U;
    unsigned int reported_csi_errors = 0U;
    unsigned int reported_recoveries = 0U;
    unsigned int reported_recovery_errors = 0U;
    unsigned int reported_isp_errors = 0U;
    std::uint32_t last_async_error_log_tick = common::Task::Now();
    common::Task::RunForever(context.cpu_task_monitor, "camera_render",
                     [] { tk_dly_tsk(1); }, [&] {
        ++loop_count;
        status = context.camera.Process();
        if (!status.Ok()) {
            status.LogStatus("camera");
            common::Task::Halt("ai: camera process failed\n");
        }
        const camera::Diagnostics snapshot = context.camera.GetDiagnostics();
        const bool async_error_changed =
            reported_pipe_errors != snapshot.dcmipp_error_count ||
            reported_camera_errors != snapshot.camera_error_count ||
            reported_csi_errors != snapshot.csi_error_count ||
            reported_isp_errors != snapshot.isp_error_count;
        if (async_error_changed &&
            ((reported_pipe_errors == 0U &&
              reported_camera_errors == 0U && reported_csi_errors == 0U &&
              reported_isp_errors == 0U) ||
             common::Task::Now() - last_async_error_log_tick >= 1000U)) {
            reported_pipe_errors = snapshot.dcmipp_error_count;
            reported_camera_errors = snapshot.camera_error_count;
            reported_csi_errors = snapshot.csi_error_count;
            reported_isp_errors = snapshot.isp_error_count;
            last_async_error_log_tick = common::Task::Now();
            UAI_LOG_WARN("camera: async errors pipe=%u sensor=%u csi=%u isp=%u dcmipp=%x csi0=%x csi1=%x pend0=%x pend1=%x code=%x sot_sync_dl0=%u sot_sync_dl1=%u sot_dl0=%u sot_dl1=%u\n",
                         reported_pipe_errors, reported_camera_errors,
                         reported_csi_errors, reported_isp_errors,
                         snapshot.dcmipp_last_status,
                         snapshot.csi_last_status,
                         snapshot.csi_last_status1,
                         snapshot.csi_last_pending_status,
                         snapshot.csi_last_pending_status1,
                         snapshot.csi_last_error_code,
                         snapshot.csi_sot_sync_dl0_count,
                         snapshot.csi_sot_sync_dl1_count,
                         snapshot.csi_sot_dl0_count,
                         snapshot.csi_sot_dl1_count);
        }
        if (reported_recoveries != snapshot.recovery_count ||
            reported_recovery_errors != snapshot.recovery_error_count) {
            reported_recoveries = snapshot.recovery_count;
            reported_recovery_errors = snapshot.recovery_error_count;
            UAI_LOG_WARN("camera: recovery attempts=%u failed=%u frames=%u vsync=%u pipe2=%u drops=%u\n",
                         reported_recoveries, reported_recovery_errors,
                         snapshot.frame_event_count, snapshot.vsync_event_count,
                         snapshot.pipe2_frame_event_count,
                         snapshot.pipe2_drop_count);
        }

        const std::uint32_t now = common::Task::Now();
        const bool inference_due = kCopyInferenceFrames &&
                                    (kInferenceMode == InferenceMode::kCopyOnly ||
                                     context.external_nor_ready) &&
                                    static_cast<std::int32_t>(now -
                                                              next_inference) >=
                                        0;

        /* Pipe2 is a separate RGB888 producer. Drain it on every camera-task
         * iteration so the two DMA buffers are returned quickly even when the
         * inference period is intentionally slow. */
        pipeline::InferenceFrame pipe2_frame{};
        const common::Error pipe2_status = context.camera.TakeCompletedInference(&pipe2_frame);
        if (pipe2_status.Ok()) {
            if (context.diagnostics.inference_input_display) {
                status = context.memory.ClaimInferenceBuffer(pipe2_frame);
                if (!status.Ok()) {
                    status.LogStatus("memory");
                    common::Task::Halt("ai: inference input display claim failed\n");
                }
                const bool log_input =
                    context.diagnostics.inference_input &&
                    (pipe2_frame.capture_sequence <= 3U ||
                     (pipe2_frame.capture_sequence % 30U) == 0U);
                if (log_input) {
                    UAI_LOG_DEBUG("ai: input display live sequence=%u buffer=%x\n",
                              static_cast<unsigned int>(
                                  pipe2_frame.capture_sequence),
                              static_cast<unsigned int>(
                                  pipe2_frame.buffer.address));
                    InspectInferenceInput(context, pipe2_frame);
                }
                status = context.lcd.ComposeInferenceAndPresent(pipe2_frame);
                if (!status.Ok() &&
                    (!status.IsRoutine() || log_input)) {
                    status.LogStatus("lcd");
                }
                const common::Error release_status =
                    context.memory.ReleaseInferenceBuffer(pipe2_frame);
                if (!release_status.Ok()) {
                    release_status.LogStatus("memory");
                    common::Task::Halt("ai: diagnostic inference release failed\n");
                }
            } else if (inference_due &&
                kInferenceMode == InferenceMode::kNpu) {
                status = context.camera.SnapshotInferenceSource(&pipe2_frame);
                if (!status.Ok()) {
                    status.LogStatus("camera");
                    const common::Error release_status =
                        context.memory.ReleaseInferenceBuffer(pipe2_frame);
                    release_status.LogStatus("memory");
                    next_inference = now + kInferencePeriod;
                    return;
                }
                if (context.diagnostics.inference_trace) {
                    UAI_LOG_DEBUG("ai: pipe2 frame queued sequence=%u buffer=%x events=%u drops=%u\n",
                              static_cast<unsigned int>(pipe2_frame.capture_sequence),
                              static_cast<unsigned int>(pipe2_frame.buffer.address),
                              snapshot.pipe2_frame_event_count,
                              snapshot.pipe2_drop_count);
                }
                context.pipeline_task.InferenceFrames().Send(pipe2_frame);
                next_inference = now + kInferencePeriod;
            } else {
                const common::Error release_status =
                    context.memory.ReleaseInferenceBuffer(pipe2_frame);
                if (!release_status.Ok()) {
                    release_status.LogStatus("memory");
                }
            }
        } else if (pipe2_status.Code() != common::ErrorCode::kNoFrame &&
                   pipe2_status.Code() != common::ErrorCode::kNoBuffer) {
            pipe2_status.LogStatus("camera");
        }
        const message_channel::DrainResult drain_result =
            context.pipeline_task.TryGetLatestResult(&active_boxes);
        if (!drain_result.error.Ok() &&
            drain_result.error.Code() != common::ErrorCode::kNoFrame) {
            drain_result.error.LogStatus("results");
        }
        if (drain_result.updated) {
            if (context.diagnostics.display_trace) {
                UAI_LOG_DEBUG("lcd: box source=ai sequence=%u capture=%u count=%u\n",
                              static_cast<unsigned int>(active_boxes.model_sequence),
                              static_cast<unsigned int>(active_boxes.capture_sequence),
                              static_cast<unsigned int>(active_boxes.person.count +
                                                         active_boxes.face.count));
                UAI_LOG_DEBUG("lcd: boxes person=%u face=%u mask_px=%u\n",
                              static_cast<unsigned int>(active_boxes.person.count),
                              static_cast<unsigned int>(active_boxes.face.count),
                              static_cast<unsigned int>(
                                  active_boxes.segmentation.mask_foreground_pixels));
            }
            /* The latest inference result is authoritative. Until it arrives,
             * keep presenting the previous result so model switching does not
             * create a blank frame. */
        }
        uai::ai::pipeline::CaptureFrame capture{};
        status = context.camera.TakeCompletedCapture(&capture);
        if (!status.Ok()) {
            if (status.Code() != common::ErrorCode::kNoFrame) {
                status.LogStatus("camera");
            }
            return;
        }

        if (context.diagnostics.camera_frame_trace) {
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                UAI_LOG_DEBUG("camera: frame captured sequence=%u buffer=%x event=%u aton_irq=%u last=%x\n",
                          static_cast<unsigned int>(capture.sequence),
                          static_cast<unsigned int>(capture.buffer.address),
                          snapshot.frame_event_count, g_aton_irq_count,
                          g_aton_last_irqs);
            }
        }
        if (context.diagnostics.camera_brightness) {
            InspectCaptureBrightness(context, capture);
        }

        /* In the live Pipe2 diagnostic mode, the LCD is reserved for the
         * actual inference input. Capture buffers are still drained below. */
        const bool display_due = !context.diagnostics.inference_input_display;
        if (display_due) {
            if (context.diagnostics.display_trace &&
                (capture.sequence <= 3U ||
                 (capture.sequence % 10U) == 0U)) {
                UAI_LOG_DEBUG("lcd: compose begin sequence=%u\n",
                          static_cast<unsigned int>(capture.sequence));
            }
            status = context.lcd.ComposeAndPresent(capture, active_boxes);
            if (!status.Ok()) {
                status.LogStatus("lcd");
                if (!status.IsRoutine()) {
                    common::Task::Halt("ai: lcd compose failed\n");
                }
            }
            else if (context.diagnostics.display_trace &&
                     (capture.sequence <= 3U ||
                      (capture.sequence % 10U) == 0U)) {
                UAI_LOG_DEBUG("lcd: frame presented sequence=%u buffer=%x\n",
                          static_cast<unsigned int>(capture.sequence),
                          static_cast<unsigned int>(capture.buffer.address));
            }
            if (context.diagnostics.display_trace &&
                (capture.sequence <= 3U ||
                 (capture.sequence % 10U) == 0U)) {
                UAI_LOG_DEBUG("lcd: compose end sequence=%u aton_irq=%u last=%x\n",
                          static_cast<unsigned int>(capture.sequence),
                          g_aton_irq_count, g_aton_last_irqs);
            }
        }

        if (context.diagnostics.camera_frame_trace &&
            (loop_count % 1000U) == 0U) {
            UAI_LOG_DEBUG("camera: heartbeat loop=%u sequence=%u pipe2=%u drops=%u aton_irq=%u last=%x\n",
                      static_cast<unsigned int>(loop_count),
                      static_cast<unsigned int>(capture.sequence),
                      snapshot.pipe2_frame_event_count,
                      snapshot.pipe2_drop_count,
                      g_aton_irq_count, g_aton_last_irqs);
        }
    });
}

} // namespace uai::ai::task
