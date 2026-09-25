#include <cstdint>
#include <cstring>

#include <tk/tkernel.h>

#include "driver/imager_driver/imager_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "task/task_context.hpp"

namespace uai::sample2::task {

void camera_render_task(void)
{
    Error status{};

    uai::sample2::LcdDriver lcd;
    uai::sample2::ImagerDriver imager;
    const BoxSet initial = EmptyBoxes();

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initialize begin\n")));
    status = lcd.Initialize(g_memory, g_memory_hardware);
    if (!status.Ok()) {
        LogStatus("lcd", status);
        Halt("sample2: lcd initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initialize result=ok\n")));
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initial frame begin\n")));
    status = lcd.ShowInitialFrame(initial,
                                  kDisplayCoordinatePatternDiagnostic);
    if (!status.Ok()) {
        LogStatus("lcd", status);
        Halt("sample2: initial frame failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "lcd: initial buffer ready box_count=0\n")));

    if constexpr (kDisplayCoordinatePatternDiagnostic) {
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: diagnostic frame did not latch\n");
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
            Halt("sample2: initial diagnostic frame did not latch\n");
        }

        std::uintptr_t capture0 = 0U;
        std::uintptr_t capture1 = 0U;
        status = g_memory.CaptureBuffers(&capture0, &capture1);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: diagnostic capture buffers unavailable\n");
        }
        const uai::sample2::memory_manager::Buffer source_buffer{
            capture0, uai::sample2::memory_manager::kFrameBytes, 0U,
            uai::sample2::memory_manager::Region::kCapture};
        status = g_memory_hardware.PrepareForDmaWrite(source_buffer);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: diagnostic source cache prepare failed\n");
        }
        status = lcd.GenerateCoordinatePattern(source_buffer);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: diagnostic pattern generation failed\n");
        }
        SCB_CleanDCache_by_Addr(
            reinterpret_cast<std::uint32_t *>(source_buffer.address),
            static_cast<std::int32_t>(source_buffer.size));

        uai::sample2::memory_manager::CaptureFrame synthetic_capture{};
        status = g_memory.ImportCompletedCapture(source_buffer.address,
                                                  &synthetic_capture);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: diagnostic capture import failed\n");
        }
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: synthetic compose begin sequence=%u source=%x bytes=%u\n"),
                  static_cast<unsigned int>(synthetic_capture.sequence),
                  static_cast<unsigned int>(source_buffer.address),
                  static_cast<unsigned int>(source_buffer.size));
        status = lcd.ComposeAndPresent(synthetic_capture, initial, true);
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: synthetic compose failed\n");
        }
        tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
            "lcd: synthetic compose presented; camera remains stopped\n")));
        for (;;) {
            tk_dly_tsk(1000);
        }
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: initialize begin\n")));
    status = imager.Initialize(g_memory, g_memory_hardware);
    if (!status.Ok()) {
        LogStatus("camera", status);
        Halt("sample2: camera initialization failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: initialize result=ok\n")));
    status = imager.Start();
    if (!status.Ok()) {
        LogStatus("camera", status);
        Halt("sample2: camera start failed\n");
    }
    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera: start result=ok detail=0\n")));
    if constexpr (kLiveCaptureFreezeDiagnostic) {
        uai::sample2::memory_manager::CaptureFrame first_capture{};
        const std::uint32_t wait_start = Now();
        for (;;) {
            status = imager.Process();
            if (!status.Ok()) {
                LogStatus("camera", status);
                Halt("sample2: frozen capture process failed\n");
            }
            uai::sample2::memory_manager::CaptureFrame candidate{};
            status = imager.TakeCompletedCapture(&candidate);
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
                Halt("sample2: frozen capture acquire failed\n");
            }
            if (static_cast<std::uint32_t>(Now() - wait_start) >= 3000U) {
                Halt("sample2: frozen capture timed out\n");
            }
            tk_dly_tsk(1);
        }
        const unsigned int events_before_stop = g_camera_frame_event_count;
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: freeze requested sequence=%u address=%x events=%u\n"),
                  static_cast<unsigned int>(first_capture.sequence),
                  static_cast<unsigned int>(first_capture.buffer.address),
                  events_before_stop);
        status = imager.Stop();
        if (!status.Ok()) {
            LogStatus("camera", status);
            Halt("sample2: camera stop failed; capture not inspected\n");
        }

        uai::sample2::memory_manager::CaptureFrame latest_capture{};
        const Error latest_status =
            imager.TakeCompletedCapture(&latest_capture);
        if (latest_status.Ok()) {
            first_capture = latest_capture;
        } else if (latest_status.code != ErrorCode::kNoFrame) {
            LogStatus("camera", latest_status);
            Halt("sample2: stopped capture acquire failed\n");
        }
        status = g_memory_hardware.PrepareForCpuRead(first_capture.buffer);
        if (!status.Ok()) {
            LogStatus("memory", status);
            Halt("sample2: frozen capture cache invalidate failed\n");
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
            Halt("sample2: frozen live capture compose failed\n");
        }
        status = lcd.SynchronizeCurrentFrame();
        if (!status.Ok()) {
            LogStatus("lcd", status);
            Halt("sample2: frozen live capture display did not latch\n");
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
    bool first_frame = true;
    std::uint32_t next_inference = 0U;

    std::uint32_t loop_count = 0U;
    unsigned int reported_pipe_errors = 0U;
    unsigned int reported_camera_errors = 0U;
    unsigned int reported_csi_errors = 0U;
    unsigned int reported_recoveries = 0U;
    unsigned int reported_recovery_errors = 0U;
    unsigned int reported_isp_errors = 0U;
    std::uint32_t last_async_error_log_tick = Now();
    std::uint32_t last_box_update = Now();
    for (;;) {
        ++loop_count;
        status = imager.Process();
        if (!status.Ok()) {
            LogStatus("camera", status);
            Halt("sample2: camera process failed\n");
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
                          "camera: recovery attempts=%u failed=%u frames=%u vsync=%u\n"),
                      reported_recoveries, reported_recovery_errors,
                      g_camera_frame_event_count, g_camera_vsync_event_count);
        }

        const std::uint32_t now = Now();
        if (DrainLatestBoxes(&active_boxes)) {
            last_box_update = now;
        } else if (active_boxes.count > 0U &&
                   static_cast<std::uint32_t>(now - last_box_update) >=
                       kBoxLifetimeMs) {
            active_boxes = EmptyBoxes();
            tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
                "lcd: stale inference boxes cleared\n")));
        }
        uai::sample2::memory_manager::CaptureFrame capture{};
        status = imager.TakeCompletedCapture(&capture);
        if (!status.Ok()) {
            if (status.code != ErrorCode::kNoFrame) {
                LogStatus("camera", status);
            }
            tk_dly_tsk(1);
            continue;
        }

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

        const bool display_due = true;
        const bool inference_due = kCopyInferenceFrames &&
                                    (kInferenceMode == InferenceMode::kCopyOnly ||
                                     g_external_nor_ready) &&
                                    (first_frame ||
                                     static_cast<std::int32_t>(now -
                                                               next_inference) >=
                                         0);
        if (display_due) {
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: compose begin sequence=%u\n"),
                          static_cast<unsigned int>(capture.sequence));
            }
            status = lcd.ComposeAndPresent(capture, active_boxes);
            if (!status.Ok()) {
                LogStatus("lcd", status);
                if (!IsBestEffort(status.code)) {
                    Halt("sample2: lcd compose failed\n");
                }
            } else if (capture.sequence <= 3U ||
                       (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: frame presented sequence=%u buffer=%x\n"),
                          static_cast<unsigned int>(capture.sequence),
                          static_cast<unsigned int>(capture.buffer.address));
            }
            if (capture.sequence <= 3U ||
                (capture.sequence % 10U) == 0U) {
                tm_printf(reinterpret_cast<const UB *>(
                              "lcd: compose end sequence=%u aton_irq=%u last=%x\n"),
                          static_cast<unsigned int>(capture.sequence),
                          g_aton_irq_count, g_aton_last_irqs);
            }
        }

        if (inference_due) {
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: snapshot begin sequence=%u\n"),
                      static_cast<unsigned int>(capture.sequence));
            InferenceFrame inference{};
            status = g_memory.AcquireInferenceBuffer(capture, &inference);
            if (status.Ok()) {
                status = g_memory_hardware.PrepareForCpuRead(capture.buffer);
            }
            if (status.Ok()) {
                const std::uint32_t copy_start = Now();
                std::memcpy(
                    reinterpret_cast<void *>(inference.buffer.address),
                    reinterpret_cast<const void *>(capture.buffer.address),
                    uai::sample2::memory_manager::kFrameBytes);
                const std::uint32_t copy_elapsed = Now() - copy_start;
                const Error generation_after_copy =
                    g_memory.ValidateCaptureFrame(capture);
                if constexpr (kInferenceMode == InferenceMode::kCopyOnly) {
                    tm_printf(reinterpret_cast<const UB *>(
                                  "ai: copy-only sequence_before=%u bytes=%u elapsed_ms=%u generation_valid_after=%u code=%u\n"),
                              static_cast<unsigned int>(capture.sequence),
                              static_cast<unsigned int>(
                                  uai::sample2::memory_manager::kFrameBytes),
                              static_cast<unsigned int>(copy_elapsed),
                              static_cast<unsigned int>(
                                  generation_after_copy.Ok() ? 1U : 0U),
                              static_cast<unsigned int>(generation_after_copy.code));
                }
                status = {ErrorCode::kOk, capture.sequence,
                          "memory.snapshot_inference"};
            } else if (inference) {
                const Error release_status =
                    g_memory.ReleaseInferenceBuffer(inference);
                LogStatus("memory", release_status);
            }
            tm_printf(reinterpret_cast<const UB *>(
                          "ai: snapshot end sequence=%u code=%u detail=%x buffer=%x\n"),
                      static_cast<unsigned int>(capture.sequence),
                      static_cast<unsigned int>(status.code),
                      static_cast<unsigned int>(status.detail),
                      static_cast<unsigned int>(inference.buffer.address));
            if (status.Ok()) {
                if constexpr (kInferenceMode == InferenceMode::kNpu) {
                    SendInferenceFrame(inference);
                    tm_printf(reinterpret_cast<const UB *>(
                                  "ai: frame copied sequence=%u buffer=%x irq=%u last=%x\n"),
                              static_cast<unsigned int>(capture.sequence),
                              static_cast<unsigned int>(inference.buffer.address),
                              g_aton_irq_count, g_aton_last_irqs);
                } else {
                    status = g_memory.ReleaseInferenceBuffer(inference);
                    if (status.Ok()) {
                        tm_printf(reinterpret_cast<const UB *>(
                                      "ai: copy-only snapshot released sequence=%u\n"),
                                  static_cast<unsigned int>(capture.sequence));
                    } else {
                        LogStatus("memory", status);
                    }
                }
            } else if (!IsBestEffort(status.code)) {
                LogStatus("memory", status);
            }
            next_inference = now + kInferencePeriod;
        }
        first_frame = false;
        if ((loop_count % 1000U) == 0U) {
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: heartbeat loop=%u sequence=%u aton_irq=%u last=%x\n"),
                      static_cast<unsigned int>(loop_count),
                      static_cast<unsigned int>(capture.sequence),
                      g_aton_irq_count, g_aton_last_irqs);
        }
        tk_dly_tsk(1);
    }
}

} // namespace uai::sample2::task
