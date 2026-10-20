#include "task/camera_render/camera_render_context_and_operations.hpp"

#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/npu_driver/debug.h"
#include "middleware/foundation/log.hpp"
#include "middleware/pipeline/image_format.hpp"
#include "middleware/task/task.hpp"
#include "task/exposure_analysis_task.hpp"
#include "ui/app_ui.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
}

namespace uai::ai::task::camera_render {

void ComposeAndPresentCameraFrameOrUiOverlay(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle
)
{
    const auto now = cycle.now;
    const auto &features = cycle.features;
    const auto &snapshot = cycle.diagnostics;
    const auto &active_boxes = state.results.boxes;
    auto &screen_ui = state.ui;
    auto &capture = cycle.capture;
    auto status = context.camera.TakeCompletedCapture(&capture);
    cycle.capture_ready = status.Ok();
    const bool capture_ready = cycle.capture_ready;
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
        LogCaptureBrightnessAndSensorExposure(context, capture);
    }

    /* In the live Pipe2 diagnostic mode, the LCD is reserved for the
     * actual inference input. Capture buffers are still drained below. */
    const bool display_due = state.ShouldPresent(capture_ready, cycle.ui_dirty, features.input_display);
    if (display_due) {
        state.schedule.StartEvent(RenderOperation::kDisplay);
        cycle.did_work = true;
        if (context.diagnostics.display_trace && (capture.sequence <= 3U || (capture.sequence % 10U) == 0U)) {
            UAI_LOG_DEBUG("lcd: compose begin sequence=%u\n", static_cast<unsigned int>(capture.sequence));
        }
        /* Screens without the camera repaint from scratch; the capture
         * is still taken above so its buffer returns to the pool. */
        const auto &visualization = screen_ui.VisualizationSettings();
        const auto *analysis = context.exposure_task.Latest(visualization, now);
        const app_ui::ExposureImagePainter image(analysis, visualization);
        const app_ui::LuminanceHistogramPainter histogram(image);
        const ui::Painter *overlays[] = {&histogram, &screen_ui.Overlay()};
        const ui::PainterGroup overlay(overlays, 2U);
        const bool visualization_enabled = analysis != nullptr;
        status = features.shows_camera ? context.lcd.ComposeAndPresent(
                                             capture,
                                             screen_ui.VisibleBoxes(active_boxes),
                                             visualization_enabled ? &overlay : &screen_ui.Overlay(),
                                             false,
                                             visualization_enabled ? &image : nullptr
                                         )
                                       : context.lcd.PresentOverlay(screen_ui.Overlay());
        auto &display = state.schedule.Stats(RenderOperation::kDisplay);
        display.Finish(status.Ok());
        if (status.Ok() && capture_ready) {
            const auto delay = HAL_GetTick() - capture.completed_ms;
            if (delay > display.max_frame_delay_ms)
                display.max_frame_delay_ms = delay;
            if (features.shows_camera && context.exposure_task.ReadyToSubmit(visualization, now)) {
                (void)context.exposure_task.TrySubmit(
                    reinterpret_cast<const std::uint16_t *>(capture.buffer.address),
                    static_cast<std::uint16_t>(pipeline::kCaptureFormat.width),
                    static_cast<std::uint16_t>(pipeline::kCaptureFormat.height),
                    visualization,
                    capture.sequence,
                    now
                );
            }
        }
        if (!status.Ok()) {
            status.LogStatus("lcd");
            if (!status.IsRoutine()) {
                common::Task::Halt("ai: lcd compose failed\n");
            }
        } else if (context.diagnostics.display_trace && (capture.sequence <= 3U || (capture.sequence % 10U) == 0U)) {
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
}

}