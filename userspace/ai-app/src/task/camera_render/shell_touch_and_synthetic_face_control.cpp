#include "task/camera_render/camera_render_context_and_operations.hpp"

#include <algorithm>
#include <cstdio>

#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/touch_driver/touch_driver.hpp"
#include "middleware/foundation/log.hpp"
#include "middleware/ui/touch_point.hpp"
#include "shell/mailbox.hpp"
#include "shell/owner.hpp"
#include "task/pipeline_task.hpp"

namespace uai::ai::task::camera_render {

namespace {

constexpr std::uint32_t kSyntheticFaceIntervalMs = 250U;

inference::BoxSet CreateSyntheticFaceDetectionBoxes(
    std::uint32_t count,
    std::uint32_t capture_sequence
)
{
    inference::BoxSet boxes{};
    boxes.model_sequence = capture_sequence;
    boxes.capture_sequence = capture_sequence;
    boxes.face_valid = true;
    boxes.face.count = std::min<std::uint32_t>(count, inference::kMaxBoxes);
    for (std::uint32_t index = 0U; index < boxes.face.count; ++index) {
        auto &box = boxes.face.boxes[index];
        box.x = static_cast<std::int16_t>(80U + (index % 4U) * 160U);
        box.y = static_cast<std::int16_t>(40U + (index / 4U) * 96U);
        box.width = 80;
        box.height = 80;
        box.confidence = 0.90F + static_cast<float>(index) * 0.005F;
    }
    return boxes;
}

}

void ResetInferenceResultsAndExposureControl(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle
)
{
    state.results.Reset(context.pipeline_task.AdvanceGeneration());
    state.exposure.Reset();
    state.schedule.ConfigureFrames(
        kInferenceFrameStride, context.camera.GetDiagnostics().pipe2_latest_capture_sequence + kInferenceFrameStride
    );
    cycle.ui_dirty = true;
    cycle.did_work = true;
}

void ProcessShellTouchAndSyntheticFaceOutput(
    const CameraRenderContext &context,
    CameraRenderState &state,
    SyntheticFaceOutputState &synthetic_face,
    CameraRenderCycleContext &cycle
)
{
    const auto &snapshot = cycle.diagnostics;
    const auto now = cycle.now;
    auto &screen_ui = state.ui;
    auto &exposure_mode = state.exposure_mode;
    auto &did_work = cycle.did_work;
    auto &ui_dirty = cycle.ui_dirty;
    shell::Request shell_request{};
    if (context.shell_mailbox.Take(&shell_request)) {
        did_work = true;
        ui_dirty = true;
        shell::Reply reply{};
        if (shell_request.action == shell::Action::kTestFaceOutput) {
            if (shell_request.values[0] != 0) {
                synthetic_face.enabled = true;
                synthetic_face.clear_pending = false;
                synthetic_face.logged = false;
                synthetic_face.count = static_cast<std::uint32_t>(shell_request.values[1]);
                synthetic_face.last_tick = now - kSyntheticFaceIntervalMs;
                synthetic_face.last_capture = snapshot.pipe2_latest_capture_sequence;
                std::snprintf(
                    reply.text,
                    sizeof(reply.text),
                    "test-face=on boxes=%lu; emitting through exposure results\r\n",
                    static_cast<unsigned long>(synthetic_face.count)
                );
            } else {
                synthetic_face.enabled = false;
                synthetic_face.clear_pending = true;
                std::snprintf(reply.text, sizeof(reply.text), "test-face=off; face hold expires in 1000ms\r\n");
            }
            UAI_LOG_INFO(
                "ai: test-face %s boxes=%u\n",
                synthetic_face.enabled ? "on" : "off",
                static_cast<unsigned int>(synthetic_face.count)
            );
        } else if (shell_request.action == shell::Action::kDiagnostics) {
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
        screen_ui.RecordOperationResult(reply.code);
        context.shell_mailbox.Complete(reply);
        if (shell_request.action == shell::Action::kCameraFps || shell_request.action == shell::Action::kCameraFlip
            || shell_request.action == shell::Action::kCameraCrop) {
            camera::Geometry geometry{};
            const auto geometry_status = context.camera.GetGeometry(&geometry);
            if (geometry_status.Ok()
                && (geometry.fps != state.geometry.fps || geometry.horizontal != state.geometry.horizontal
                    || geometry.vertical != state.geometry.vertical || geometry.crop.x != state.geometry.crop.x
                    || geometry.crop.y != state.geometry.crop.y || geometry.crop.width != state.geometry.crop.width
                    || geometry.crop.height != state.geometry.crop.height)) {
                state.geometry = geometry;
                ResetInferenceResultsAndExposureControl(context, state, cycle);
            }
        }
    }
    if (synthetic_face.enabled || synthetic_face.clear_pending) {
        const std::uint32_t capture_sequence = snapshot.pipe2_latest_capture_sequence;
        const bool have_new_frame = static_cast<std::int32_t>(capture_sequence - synthetic_face.last_capture) > 0;
        const bool due = synthetic_face.clear_pending
            || (synthetic_face.last_tick == 0U || now - synthetic_face.last_tick >= kSyntheticFaceIntervalMs);
        if (have_new_frame && due) {
            const std::uint32_t count = synthetic_face.enabled ? synthetic_face.count : 0U;
            const inference::BoxSet boxes = CreateSyntheticFaceDetectionBoxes(count, capture_sequence);
            synthetic_face.update_sequence =
                std::max(synthetic_face.update_sequence, state.results.boxes.model_sequence) + 1U;
            ModelResultSnapshot display_result{};
            display_result.boxes = boxes;
            display_result.boxes.model_sequence = synthetic_face.update_sequence;
            display_result.stamps[1] = {true, now, synthetic_face.update_sequence};
            display_result.generation = context.pipeline_task.Generation();
            const common::Error display_status = context.pipeline_task.PublishResult(display_result);
            if (!display_status.Ok())
                display_status.LogStatus("test-face-results");
            const common::Error publish_status =
                context.pipeline_task.PublishExposureResult(boxes, context.pipeline_task.Generation());
            if (publish_status.Ok()) {
                synthetic_face.last_capture = capture_sequence;
                synthetic_face.last_tick = now;
                if (synthetic_face.enabled && !synthetic_face.logged) {
                    UAI_LOG_INFO(
                        "ai: test-face output seq=%u boxes=%u\n",
                        static_cast<unsigned int>(capture_sequence),
                        static_cast<unsigned int>(boxes.face.count)
                    );
                    synthetic_face.logged = true;
                }
                if (synthetic_face.clear_pending) {
                    synthetic_face.clear_pending = false;
                    UAI_LOG_INFO("ai: test-face empty output seq=%u\n", static_cast<unsigned int>(capture_sequence));
                }
            } else {
                publish_status.LogStatus("test-face-output");
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
            if (shell::ApplyTouch(event, context.camera, screen_ui, exposure_mode, &touch_reply)) {
                screen_ui.RecordOperationResult(touch_reply.code);
                if (touch_reply.code != 0)
                    UAI_LOG_WARN("ui: apply failed code=%d\n", static_cast<int>(touch_reply.code));
            }
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
}

}