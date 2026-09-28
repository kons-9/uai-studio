#pragma once

#include <cstddef>
#include <cstdint>

#include <tk/tkernel.h>

#include "common/error.hpp"
#include "driver/npu_driver/debug.h"
#include "driver/npu_driver/npu_driver.hpp"
#include "driver/cache_driver/cache_driver.hpp"
#include "driver/camera_driver/camera_driver.hpp"
#include "driver/lcd_driver/lcd_driver.hpp"
#include "driver/nor_driver/nor_driver.hpp"
#include "driver/psram_driver/psram_driver.hpp"
#include "driver/rif_driver/rif_driver.hpp"
#include "memory_allocator/memory_allocator.hpp"
#include "task/task_diagnostics.hpp"

extern "C" {
#include <tm/tmonitor.h>
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
/* Camera diagnostics are owned by the ai camera driver. */
extern volatile unsigned int g_camera_vsync_event_count;
extern volatile unsigned int g_camera_frame_event_count;
extern volatile unsigned int g_camera_recovery_count;
extern volatile unsigned int g_camera_recovery_error_count;
extern volatile unsigned int g_camera_isp_error_count;
extern volatile unsigned int g_camera_dcmipp_last_status;
extern volatile unsigned int g_camera_dcmipp_error_count;
extern volatile unsigned int g_camera_camera_error_count;
extern volatile unsigned int g_camera_pipe2_frame_event_count;
extern volatile unsigned int g_camera_pipe2_drop_count;
extern volatile unsigned int g_camera_csi_last_status;
extern volatile unsigned int g_camera_csi_last_status1;
extern volatile unsigned int g_camera_csi_last_pending_status;
extern volatile unsigned int g_camera_csi_last_pending_status1;
extern volatile unsigned int g_camera_csi_error_count;
extern volatile unsigned int g_camera_csi_last_error_code;
extern volatile unsigned int g_camera_csi_sot_sync_dl0_count;
extern volatile unsigned int g_camera_csi_sot_sync_dl1_count;
extern volatile unsigned int g_camera_csi_sot_dl0_count;
extern volatile unsigned int g_camera_csi_sot_dl1_count;
extern volatile unsigned int g_ai_last_exposure_request_us;
extern volatile unsigned int g_ai_last_exposure_lines;
int32_t AiGetSensorGainMdB(void);
int32_t AiReadSensorRegisters(std::uint32_t *vmax,
                                   std::uint32_t *shutter,
                                   std::uint32_t *gain);
#if defined(AI_MODEL_SEGMENTATION)
void NPU0_IRQHandler(UINT intno);
#else
void NPU0_IRQHandler(void);
#endif
}

namespace uai::ai::task {

using uai::ai::common::Error;
using uai::ai::common::ErrorCode;
using BoxSet = uai::ai::memory_allocator::BoxSet;
using InferenceFrame = uai::ai::memory_allocator::InferenceFrame;
using MemoryAllocator = uai::ai::memory_allocator::MemoryAllocator;
inline constexpr UINT kExternalMemoryReady = 0x01U;
/* The NPU is asynchronous, so inference is scheduled independently from
 * Pipe1 rendering.  The measured model time is well below 100 ms, so use a
 * 20 ms submission period while retaining headroom for the camera and LCD
 * tasks. Pipe2's frame-rate divider remains the upper bound in practice. */
inline constexpr std::uint32_t kInferencePeriod = 20U;
#if defined(AI_MODEL_SEGMENTATION)
inline constexpr std::uint32_t kBoxLifetimeMs = 1000U;
#else
inline constexpr std::uint32_t kBoxLifetimeMs = 1000U;
#endif

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
#if defined(AI_INFERENCE_INPUT_DISPLAY_DIAGNOSTIC)
inline constexpr bool kInferenceInputDisplayDiagnostic =
    AI_INFERENCE_INPUT_DISPLAY_DIAGNOSTIC != 0;
#else
inline constexpr bool kInferenceInputDisplayDiagnostic = false;
#endif
inline constexpr bool kCopyInferenceFrames =
    kInferenceMode != InferenceMode::kDisabled;
inline constexpr std::size_t kFrameQueueDepth = 4U;
inline constexpr std::size_t kBoxQueueDepth = 4U;
inline constexpr SZ kInitializationTaskStackSize = 32U * 1024U;
inline constexpr SZ kCameraTaskStackSize = 32U * 1024U;
inline constexpr SZ kInferenceTaskStackSize = 16U * 1024U;

struct InferenceMessage {
    InferenceFrame frame{};
};

struct BoxMessage {
    BoxSet boxes{};
};

extern MemoryAllocator g_memory;
extern uai::ai::cache::CacheDriver g_cache;
extern uai::ai::psram::PsramDriver g_psram;
extern uai::ai::nor::NorDriver g_nor;
extern uai::ai::rif::RifDriver g_rif;
extern uai::ai::lcd::LcdDriver g_lcd;
extern uai::ai::camera::CameraDriver g_camera;
extern volatile std::uint32_t g_app_stage;
extern volatile bool g_external_nor_ready;
extern ID g_external_memory_ready;
extern ID g_frame_queue;
extern ID g_box_queue;
extern UB g_frame_queue_storage[sizeof(InferenceMessage) * kFrameQueueDepth];
extern UB g_box_queue_storage[sizeof(BoxMessage) * kBoxQueueDepth];
extern INT g_initialization_task_stack[
    kInitializationTaskStackSize / sizeof(INT)];
extern INT g_camera_task_stack[kCameraTaskStackSize / sizeof(INT)];
extern INT g_inference_task_stack[kInferenceTaskStackSize / sizeof(INT)];

[[noreturn]] void Halt(const char *message);
bool IsBestEffort(ErrorCode code);
std::uint32_t Now();
BoxSet EmptyBoxes();
bool DrainLatestBoxes(BoxSet *active);
void SendLatestBoxes(const BoxSet &boxes);
void SendInferenceFrame(const InferenceFrame &frame);
void LogInferenceInput(const InferenceFrame &frame);
void ConfigureReferenceInterruptPriorities();
Error InitializeDrivers();

void StartTask(FP entry, INT *stack, SZ stack_size, PRI priority,
               const char *name);
void application_initialize_task(void);
void camera_render_task(void);
void inference_task(void);

} // namespace uai::ai::task
