#pragma once

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "common/error.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/camera_driver/camera_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/nor_driver/nor_driver.hpp"
#include "driver/psram_driver/psram_driver.hpp"
#include "driver/rif_driver/rif_driver.hpp"
#include "application/pipeline/frame_types.hpp"
#include "middleware/cpu_task_monitor/cpu_task_monitor.hpp"
#include "memory_manager/memory_manager.hpp"
#include "models/inference_result_types.hpp"

namespace uai::ai::task {

inline constexpr UINT kExternalMemoryReady = 0x01U;
/* Dispatch every completed Pipe2 frame immediately. The NPU task already
 * blocks on completion and consumes a prefetched frame without an extra
 * delay; this value only prevents the camera task from adding a software
 * interval between queued inference frames. */
inline constexpr std::uint32_t kInferencePeriod = 0U;

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
    bool camera_frame_trace = true;
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
inline constexpr std::size_t kBoxQueueDepth = 4U;
inline constexpr SZ kInitializationTaskStackSize = 32U * 1024U;
inline constexpr SZ kCameraTaskStackSize = 32U * 1024U;
inline constexpr SZ kPipelineTaskStackSize = 16U * 1024U;
inline constexpr SZ kPipelinePostprocessTaskStackSize = 16U * 1024U;

struct InferenceMessage {
    pipeline::InferenceFrame frame{};
};

struct BoxMessage {
    inference::BoxSet boxes{};
};

/* Owns all application-wide resources shared by the task objects. The
 * resources are exposed as members because their ownership is explicit here;
 * tasks no longer depend on unrelated linker-visible global variables. */
class TaskContext final {
public:
    TaskContext();

    [[noreturn]] void Halt(const char *message);
    bool IsBestEffort(common::ErrorCode code) const;
    std::uint32_t Now() const;

    common::Error InitializeDrivers();
    void ConfigureReferenceInterruptPriorities();
    void CreateKernelObjects();
    void StartApplicationTask(FP entry);
    void StartCameraTask(FP entry);
    void StartFrameTask(FP entry);
    void StartPreprocessTask(FP entry);
    void StartNpuTask(FP entry);
    void StartPostprocessTask(FP entry);

    bool DrainLatestBoxes(inference::BoxSet *active);
    void SendLatestBoxes(const inference::BoxSet &boxes);
    void SendInferenceFrame(const pipeline::InferenceFrame &frame);

    memory_manager::MemoryManager memory;
    uai::ai::cache::CacheManagement &cache;
    uai::ai::psram::PsramManagement &psram;
    uai::ai::rif::RifManagement &rif;
    uai::ai::lcd::LcdManagement &lcd;
    uai::ai::camera::CameraManagement &camera;
    uai::ai::middleware::cpu_task_monitor::CpuTaskMonitor cpu_task_monitor;

    volatile std::uint32_t app_stage;
    volatile bool external_nor_ready;
    ID external_memory_ready;
    ID pipeline_work_ready;
    ID frame_queue;
    ID box_queue;
    DiagnosticsConfig diagnostics;

private:
    void StartTask(FP entry, INT *stack, SZ stack_size, PRI priority,
                   const char *name);

    alignas(8) UB frame_queue_storage[
        sizeof(InferenceMessage) * kFrameQueueDepth];
    alignas(8) UB box_queue_storage[sizeof(BoxMessage) * kBoxQueueDepth];
    INT initialization_task_stack[
        kInitializationTaskStackSize / sizeof(INT)];
    INT camera_task_stack[kCameraTaskStackSize / sizeof(INT)];
    INT pipeline_frame_task_stack[kPipelineTaskStackSize / sizeof(INT)];
    INT pipeline_preprocess_task_stack[kPipelineTaskStackSize / sizeof(INT)];
    INT pipeline_npu_task_stack[kPipelineTaskStackSize / sizeof(INT)];
    INT pipeline_postprocess_task_stack[
        kPipelinePostprocessTaskStackSize / sizeof(INT)];
};

/* The storage is private to task_context.cpp; this function is the only
 * application-wide access point exported by the task layer. */
TaskContext &GetTaskContext();

} // namespace uai::ai::task
