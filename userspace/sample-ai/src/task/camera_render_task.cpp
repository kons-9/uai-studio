#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

#include "driver/camera_driver/camera_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "task/task_context.hpp"

namespace uai::ai::task {

void camera_render_task(void)
{
    Error status{};

    auto &lcd = g_lcd;
    auto &camera = g_camera;
    const BoxSet initial = EmptyBoxes();

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: driver ready\n")));
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initial frame begin\n")));
    status = lcd.ShowInitialFrame(initial,
                                  kDisplayCoordinatePatternDiagnostic);
    if (!status.Ok()) {
        LogStatus("lcd", status);
        Halt("ai: initial frame failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initial buffer ready box_count=0\n")));

    if constexpr (kDisplayCoordinatePatternDiagnostic) {
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("ai: diagnostic frame did not latch\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: diagnostic readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n"),
                  static_cast<unsigned int>(LTDC_Layer1->CFBAR),
                  static_cast<unsigned int>(LTDC_Layer1->CR),
                  static_cast<unsigned int>(LTDC_Layer1->PFCR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
                  static_cast<unsigned int>(LTDC->AWCR),
                  static_cast<unsigned int>(LTDC->TWCR),
                  static_cast<unsigned int>(LTDC->ISR));
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "lcd: static coordinate pattern active; camera remains stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    if constexpr (kSyntheticComposeDiagnostic) {
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("ai: initial diagnostic frame did not latch\n");
        }

        std::uintptr_t capture0 = 0U;
        std::uintptr_t capture1 = 0U;
        status = g_memory.CaptureBuffers(&capture0, &capture1);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("ai: diagnostic capture buffers unavailable\n");
        }
        const uai::ai::memory_allocator::Buffer source_buffer{
            capture0, uai::ai::memory_allocator::kFrameBytes, 0U,
            uai::ai::memory_allocator::Region::kCapture};
        status = g_cache.PrepareForDmaWrite(source_buffer);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("ai: diagnostic source cache prepare failed\n");
        }
        status = lcd.GenerateCoordinatePattern(source_buffer);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("ai: diagnostic pattern generation failed\n");
        }
        SCB_CleanDCache_by_Addr(
            reinterpret_cast<std::uint32_t *>(source_buffer.address),
            static_cast<std::int32_t>(source_buffer.size));

        uai::ai::memory_allocator::CaptureFrame synthetic_capture{};
        status = g_memory.ImportCompletedCapture(source_buffer.address,
                                                  &synthetic_capture);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("ai: diagnostic capture import failed\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: synthetic compose begin sequence=%u source=%x bytes=%u\n"),
                  static_cast<unsigned int>(synthetic_capture.sequence),
                  static_cast<unsigned int>(source_buffer.address),
                  static_cast<unsigned int>(source_buffer.size));
        status = lcd.ComposeAndPresent(synthetic_capture, initial, true);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("ai: synthetic compose failed\n");
        }
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "lcd: synthetic compose presented; camera remains stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: driver ready\n")));
    status = camera.Start();
    if (!status.Ok()) {
        LogStatus("camera", status);
        Halt("ai: camera start failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: start result=ok detail=0\n")));
    if constexpr (kLiveCaptureFreezeDiagnostic) {
        uai::ai::memory_allocator::CaptureFrame first_capture{};
        const std::uint32_t wait_start = Now();
        for (;;) {
            status = camera.Process();
            if (!status.Ok()) {
                LogStatus("camera", status);
                Halt("ai: frozen capture process failed\n");
            }
            uai::ai::memory_allocator::CaptureFrame candidate{};
            status = camera.TakeCompletedCapture(&candidate);
            if (status.Ok()) {
                if ((candidate.sequence % 10U) == 0U) {
                    LogFrameBrightness(candidate);
                    tm_printf(reinterpret_cast<const UB *>(
                                  "camera: freeze warmup sequence=%u events=%u buffer=%x\n"),
                              static_cast<unsigned int>(candidate.sequence),
                              g_camera_frame_event_count,
                              static_cast<unsigned int>(candidate.buffer.address));
                }
                if (candidate.sequence >= 30U) {
                    first_capture = candidate;
                    break;
                }
                continue;
            }
            if (status.code != ErrorCode::kNoFrame) {
                LogStatus("camera", status);
                Halt("ai: frozen capture acquire failed\n");
            }
            if (static_cast<std::uint32_t>(Now() - wait_start) >= 3000U) {
                Halt("ai: frozen capture timed out\n");
            }
            tk_dly_tsk(1);
        }
        const unsigned int events_before_stop = g_camera_frame_event_count;
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: freeze requested sequence=%u address=%x events=%u\n"),
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address),
                  events_before_stop);
        status = camera.Stop();
        if (!status.Ok()) {
            LogStatus("camera", status);
            Halt("ai: camera stop failed; capture not inspected\n");
        }

        uai::ai::memory_allocator::CaptureFrame latest_capture{};
        const Error latest_status =
            camera.TakeCompletedCapture(&latest_capture);
        if (latest_status.Ok()) {
            first_capture = latest_capture;
        } else if (latest_status.code != ErrorCode::kNoFrame) {
            LogStatus("camera", latest_status);
            Halt("ai: stopped capture acquire failed\n");
        }
        status = g_cache.PrepareForCpuRead(first_capture.buffer);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("ai: frozen capture cache invalidate failed\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: freeze complete events_before=%u events_after=%u selected_sequence=%u selected_address=%x\n"),
                  events_before_stop, g_camera_frame_event_count,
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address));
        DumpFrozenCapture(first_capture);

        status = lcd.ComposeAndPresent(first_capture, initial, true);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("ai: frozen live capture compose failed\n");
        }
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("ai: frozen live capture display did not latch\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: frozen capture readback fb=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x awcr=%x twcr=%x isr=%x\n"),
                  static_cast<unsigned int>(LTDC_Layer1->CFBAR),
                  static_cast<unsigned int>(LTDC_Layer1->CR),
                  static_cast<unsigned int>(LTDC_Layer1->PFCR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLR),
                  static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
                  static_cast<unsigned int>(LTDC->AWCR),
                  static_cast<unsigned int>(LTDC->TWCR),
                  static_cast<unsigned int>(LTDC->ISR));
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "camera: frozen raw capture displayed; camera stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: irq dcmipp=%u/%u csi=%u/%u\n"),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(DCMIPP_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(DCMIPP_IRQn)),
              static_cast<unsigned int>(NVIC_GetEnableIRQ(CSI_IRQn)),
              static_cast<unsigned int>(NVIC_GetPendingIRQ(CSI_IRQn)));

    BoxSet active_boxes = initial;
    std::uint32_t next_inference = Now() + kInferencePeriod;

    std::uint32_t loop_count = 0U;
    unsigned int reported_pipe_errors = 0U;
    unsigned int reported_camera_errors = 0U;
    unsigned int reported_csi_errors = 0U;
    unsigned int reported_recoveries = 0U;
    unsigned int reported_recovery_errors = 0U;
    unsigned int reported_isp_errors = 0U;
    std::uint32_t last_async_error_log_tick = Now();
    for (;;) {
        ++loop_count;
        /* Pipe2 belongs to this task.  Reconfiguring it from the inference
         * task races with Process()/TakeCompletedInference() and can leave
         * DCMIPP in a state where the camera recovery path is triggered.
         * Complete the request here, before the next camera poll. */
        if (g_camera_reconfigure_complete !=
            g_camera_reconfigure_request) {
            const std::uint32_t request = g_camera_reconfigure_request;
            const auto requested_kind = static_cast<
                uai::ai::models::ModelKind>(
                g_camera_reconfigure_kind);
            const Error reconfigure_status = camera.ReconfigureInference(
                uai::ai::models::DescriptorFor(requested_kind));
            g_camera_reconfigure_code =
                static_cast<std::uint32_t>(reconfigure_status.code);
            g_camera_reconfigure_detail = reconfigure_status.detail;
            /* Publish completion last so the inference task never observes
             * a request as complete before its result fields are written. */
            g_camera_reconfigure_complete = request;
        }
        status = camera.Process();
        if (!status.Ok()) {
            LogStatus("camera", status);
            Halt("ai: camera process failed\n");
        }
        const bool async_error_changed =
            reported_pipe_errors != g_camera_dcmipp_error_count ||
            reported_camera_errors != g_camera_camera_error_count ||
            reported_csi_errors != g_camera_csi_error_count ||
            reported_isp_errors != g_camera_isp_error_count;
        if (async_error_changed &&
            ((reported_pipe_errors == 0U &&
              reported_camera_errors == 0U && reported_csi_errors == 0U &&
              reported_isp_errors == 0U) ||
             Now() - last_async_error_log_tick >= 1000U)) {
            reported_pipe_errors = g_camera_dcmipp_error_count;
            reported_camera_errors = g_camera_camera_error_count;
            reported_csi_errors = g_camera_csi_error_count;
            reported_isp_errors = g_camera_isp_error_count;
            last_async_error_log_tick = Now();
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: async errors pipe=%u sensor=%u csi=%u isp=%u dcmipp=%x csi0=%x csi1=%x pend0=%x pend1=%x code=%x sot_sync_dl0=%u sot_sync_dl1=%u sot_dl0=%u sot_dl1=%u\n"),
                      reported_pipe_errors, reported_camera_errors,
                      reported_csi_errors, reported_isp_errors,
                      g_camera_dcmipp_last_status,
                      g_camera_csi_last_status,
                      g_camera_csi_last_status1,
                      g_camera_csi_last_pending_status,
                      g_camera_csi_last_pending_status1,
                      g_camera_csi_last_error_code,
                      g_camera_csi_sot_sync_dl0_count,
                      g_camera_csi_sot_sync_dl1_count,
                      g_camera_csi_sot_dl0_count,
                      g_camera_csi_sot_dl1_count);
        }
        if (reported_recoveries != g_camera_recovery_count ||
            reported_recovery_errors != g_camera_recovery_error_count) {
            reported_recoveries = g_camera_recovery_count;
            reported_recovery_errors = g_camera_recovery_error_count;
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: recovery attempts=%u failed=%u frames=%u vsync=%u pipe2=%u drops=%u\n"),
                      reported_recoveries, reported_recovery_errors,
                      g_camera_frame_event_count, g_camera_vsync_event_count,
                      g_camera_pipe2_frame_event_count,
                      g_camera_pipe2_drop_count);
        }

        const std::uint32_t now = Now();
        const bool inference_due = kCopyInferenceFrames &&
                                    (kInferenceMode == InferenceMode::kCopyOnly ||
                                     g_external_nor_ready) &&
                                    static_cast<std::int32_t>(now -
                                                              next_inference) >=
                                        0;

        /* Pipe2 is a separate RGB888 producer. Drain it on every camera-task
         * iteration so the two DMA buffers are returned quickly even when the
         * inference period is intentionally slow. */
        InferenceFrame pipe2_frame{};
        const Error pipe2_status = camera.TakeCompletedInference(&pipe2_frame);
        if (pipe2_status.Ok() && g_model_switch_in_progress) {
            /* Do not queue frames in the old tensor format while the model
             * and Pipe2 are being switched. */
            status = g_memory.ReleaseInferenceBuffer(pipe2_frame);
            if (!status.Ok()) {
                LogStatus("memory", status);
            }
        } else if (pipe2_status.Ok()) {
            if constexpr (kInferenceInputDisplayDiagnostic) {
                status = g_memory.ClaimInferenceBuffer(pipe2_frame);
                if (!status.Ok()) {
                    LogStatus("memory", status);
                    Halt("ai: inference input display claim failed\n");
                }
                const bool log_input =
                    pipe2_frame.capture_sequence <= 3U ||
                    (pipe2_frame.capture_sequence % 30U) == 0U;
                if (log_input) {
                    tm_printf(reinterpret_cast<const UB *>(
                                  "ai: input display live sequence=%u buffer=%x\n"),
                              static_cast<unsigned int>(
                                  pipe2_frame.capture_sequence),
                              static_cast<unsigned int>(
                                  pipe2_frame.buffer.address));
                    LogInferenceInput(pipe2_frame);
                }
                status = lcd.ComposeInferenceAndPresent(pipe2_frame);
                if (!status.Ok() &&
                    (!IsBestEffort(status.code) || log_input)) {
                    LogStatus("lcd", status);
                }
                const Error release_status =
                    g_memory.ReleaseInferenceBuffer(pipe2_frame);
                if (!release_status.Ok()) {
                    LogStatus("memory", release_status);
                    Halt("ai: diagnostic inference release failed\n");
                }
            } else if (inference_due &&
                kInferenceMode == InferenceMode::kNpu) {
#if AI_INFERENCE_DIAGNOSTICS
                tm_printf(reinterpret_cast<const UB *>(
                              "ai: pipe2 frame queued sequence=%u buffer=%x events=%u drops=%u\n"),
                          static_cast<unsigned int>(pipe2_frame.capture_sequence),
                          static_cast<unsigned int>(pipe2_frame.buffer.address),
                          g_camera_pipe2_frame_event_count,
                          g_camera_pipe2_drop_count);
#endif
                SendInferenceFrame(pipe2_frame);
                next_inference = now + kInferencePeriod;
            } else {
                const Error release_status =
                    g_memory.ReleaseInferenceBuffer(pipe2_frame);
                if (!release_status.Ok()) {
                    LogStatus("memory", release_status);
                }
            }
        } else if (pipe2_status.code != ErrorCode::kNoFrame &&
                   pipe2_status.code != ErrorCode::kNoBuffer) {
            LogStatus("camera", pipe2_status);
        }
        if (DrainLatestBoxes(&active_boxes)) {
            /* The latest inference result is authoritative. Until it arrives,
             * keep presenting the previous result so model switching does not
             * create a blank frame. */
        }
        uai::ai::memory_allocator::CaptureFrame capture{};
        status = camera.TakeCompletedCapture(&capture);
        if (!status.Ok()) {
            if (status.code != ErrorCode::kNoFrame) {
                LogStatus("camera", status);
            }
            tk_dly_tsk(1);
            continue;
        }

#if AI_INFERENCE_DIAGNOSTICS
        if (capture.sequence <= 3U ||
            (capture.sequence % 10U) == 0U) {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: frame captured sequence=%u buffer=%x event=%u aton_irq=%u last=%x\n"),
                      static_cast<unsigned int>(capture.sequence),
                      static_cast<unsigned int>(capture.buffer.address),
                      g_camera_frame_event_count, g_aton_irq_count,
                      g_aton_last_irqs);
        }
        LogFrameBrightness(capture);
#endif

        /* In the live Pipe2 diagnostic mode, the LCD is reserved for the
         * actual inference input. Capture buffers are still drained below. */
        const bool display_due = !kInferenceInputDisplayDiagnostic;
        if (display_due) {
#if AI_INFERENCE_DIAGNOSTICS
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: compose begin sequence=%u\n"),
                          static_cast<unsigned int>(capture.sequence));
            }
#endif
            status = lcd.ComposeAndPresent(capture, active_boxes);
            if (!status.Ok()) {
                LogStatus("lcd", status);
                if (!IsBestEffort(status.code)) {
                    Halt("ai: lcd compose failed\n");
                }
            }
#if AI_INFERENCE_DIAGNOSTICS
            else if (capture.sequence <= 3U ||
                       (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: frame presented sequence=%u buffer=%x\n"),
                          static_cast<unsigned int>(capture.sequence),
                          static_cast<unsigned int>(capture.buffer.address));
            }
#endif
#if AI_INFERENCE_DIAGNOSTICS
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: compose end sequence=%u aton_irq=%u last=%x\n"),
                          static_cast<unsigned int>(capture.sequence),
                          g_aton_irq_count, g_aton_last_irqs);
            }
#endif
        }

#if AI_INFERENCE_DIAGNOSTICS
        if ((loop_count % 1000U) == 0U) {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: heartbeat loop=%u sequence=%u pipe2=%u drops=%u aton_irq=%u last=%x\n"),
                      static_cast<unsigned int>(loop_count),
                      static_cast<unsigned int>(capture.sequence),
                      g_camera_pipe2_frame_event_count,
                      g_camera_pipe2_drop_count,
                      g_aton_irq_count, g_aton_last_irqs);
        }
#endif
        tk_dly_tsk(1);
    }
}

} // namespace uai::ai::task
