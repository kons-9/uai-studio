#pragma once

#include "driver/camera_driver/camera_driver.hpp"
#include "middleware/pipeline/frame_types.hpp"
#include "task/camera_render_state.hpp"
#include "task/camera_render_task.hpp"

namespace uai::ai::task::camera_render {

struct CameraRenderFeatureSnapshot {
    bool input_display = false;
    bool inference_trace = false;
    bool frame_trace = false;
    bool brightness = false;
    bool display_trace = false;
    bool shows_camera = true;
    bool ai_exposure = false;
    std::uint8_t model_mask = 0U;
};

struct CameraRenderCycleContext {
    explicit CameraRenderCycleContext(const camera::Diagnostics &snapshot) : diagnostics(snapshot) {}
    CameraRenderCycleContext(const CameraRenderCycleContext &) = delete;
    CameraRenderCycleContext &operator=(const CameraRenderCycleContext &) = delete;

    const camera::Diagnostics diagnostics;
    std::uint32_t now = 0U;
    pipeline::InferenceFrame pipe2{};
    pipeline::CaptureFrame capture{};
    CameraRenderFeatureSnapshot features{};
    bool did_work = false;
    bool ui_dirty = false;
    bool capture_ready = false;
};

struct SyntheticFaceOutputState {
    bool enabled = false;
    bool clear_pending = false;
    bool logged = false;
    std::uint32_t count = 0U;
    std::uint32_t last_tick = 0U;
    std::uint32_t last_capture = 0U;
    std::uint32_t update_sequence = 0U;
};

void ShowInitialDisplayAndStartCamera(
    const CameraRenderContext &context,
    const inference::BoxSet &initial
);
void ResetInferenceResultsAndExposureControl(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle
);
void ProcessShellTouchAndSyntheticFaceOutput(
    const CameraRenderContext &context,
    CameraRenderState &state,
    SyntheticFaceOutputState &synthetic_face,
    CameraRenderCycleContext &cycle
);
void HandleCameraRecoveryAndReportHealth(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle
);
void SubmitPipe2FramesForInferenceOrDisplay(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle,
    CameraRenderWake &wake
);
void ReceiveInferenceResultsAndUpdateExposureControl(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle,
    CameraRenderWake &wake
);
void ComposeAndPresentCameraFrameOrUiOverlay(
    const CameraRenderContext &context,
    CameraRenderState &state,
    CameraRenderCycleContext &cycle
);
void CountIdleWakeupsAndReportRenderStatistics(
    CameraRenderState &state,
    const CameraRenderCycleContext &cycle
);
void LogCaptureBrightnessAndSensorExposure(
    const CameraRenderContext &context,
    const pipeline::CaptureFrame &capture
);
void LogRenderOperationScheduleStatistics(const CameraRenderState &state);

}