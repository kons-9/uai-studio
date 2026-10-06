#pragma once

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

namespace uai::ai::task {

inline constexpr UINT kExternalMemoryReady = 0x01U;
/* Dispatch every completed Pipe2 frame immediately. The NPU task already
 * blocks on completion and consumes a prefetched frame without an extra
 * delay; this value only prevents the camera task from adding a software
 * interval between queued inference frames. */
inline constexpr std::uint32_t kInferencePeriod = 0U;
/* GT911 state is read over I2C; 10 ms keeps taps responsive without adding
 * bus traffic on every 1 ms camera-task iteration. */
inline constexpr std::uint32_t kTouchPollPeriod = 10U;

enum class InferenceMode : std::uint8_t {
    kDisabled,
    kCopyOnly,
    kNpu,
};

enum class DisplayDiagnosticMode : std::uint8_t {
    kCameraPreview,
    kStaticPattern,
    kSyntheticCompose,
    kLiveCaptureFreeze,
};

inline constexpr InferenceMode kInferenceMode = InferenceMode::kNpu;
inline constexpr DisplayDiagnosticMode kDisplayDiagnosticMode =
    DisplayDiagnosticMode::kCameraPreview;
inline constexpr bool kDisplayCoordinatePatternDiagnostic =
    kDisplayDiagnosticMode == DisplayDiagnosticMode::kStaticPattern;
inline constexpr bool kSyntheticComposeDiagnostic =
    kDisplayDiagnosticMode == DisplayDiagnosticMode::kSyntheticCompose;
inline constexpr bool kLiveCaptureFreezeDiagnostic =
    kDisplayDiagnosticMode == DisplayDiagnosticMode::kLiveCaptureFreeze;
inline constexpr bool kCopyInferenceFrames =
    kInferenceMode != InferenceMode::kDisabled;

/* Runtime diagnostics. Per-frame diagnostics remain disabled in normal
 * operation; the low-rate inference_fps aggregate is enabled to expose
 * Pipe2 drops and inference freshness without per-frame UART traffic. */
struct DiagnosticsConfig {
    bool register_dump = false;
    bool camera_frame_trace = false;
    bool camera_brightness = false;
    bool inference_input = false;
    bool inference_input_display = false;
    bool inference_trace = false;
    /* Keep the low-rate aggregate enabled while measuring Pipe2/NPU
     * freshness. Per-frame trace remains opt-in because UART is intrusive. */
    bool inference_fps = true;
    bool display_trace = false;
    bool display_timing = false;
};

inline constexpr std::size_t kFrameQueueDepth = 4U;
inline constexpr std::size_t kResultQueueDepth = 4U;
inline constexpr SZ kInitializationTaskStackSize = 32U * 1024U;
inline constexpr SZ kCameraTaskStackSize = 32U * 1024U;
inline constexpr SZ kPipelineTaskStackSize = 16U * 1024U;
inline constexpr SZ kPipelinePostprocessTaskStackSize = 16U * 1024U;

} // namespace uai::ai::task