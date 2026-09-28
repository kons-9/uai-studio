#include "driver/camera_driver/camera_driver.hpp"

#include "driver/camera_driver/sensor_driver/registers/imx335_registers.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
#include "stm32n6570_discovery_camera.h"
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern ISP_HandleTypeDef hcamera_isp;
ISP_StatusTypeDef AiSetImx335Exposure(uint32_t instance, int32_t exposure);
ISP_StatusTypeDef AiGetImx335Exposure(uint32_t instance, int32_t *exposure);
ISP_StatusTypeDef AiSetImx335Gain(uint32_t instance, int32_t gain_mdB);
ISP_StatusTypeDef AiGetImx335Gain(uint32_t instance, int32_t *gain_mdB);
void AiResetImx335ControlState(void);
}

namespace {

using uai::ai::common::Error;
using uai::ai::common::ErrorCode;
using uai::ai::camera::sensor::registers::Imx335RegisterLayer;

constexpr std::uint32_t kSensorWidth = 2592U;
constexpr std::uint32_t kSensorHeight = 1944U;
constexpr std::uint32_t kOutputWidth = 800U;
constexpr std::uint32_t kOutputHeight = 480U;
#if defined(AI_MODEL_SEGMENTATION)
constexpr std::uint32_t kInferenceWidth = 320U;
constexpr std::uint32_t kInferenceHeight = 320U;
#else
constexpr std::uint32_t kInferenceWidth = 480U;
constexpr std::uint32_t kInferenceHeight = 480U;
#endif
constexpr std::uint32_t kFrameTimeoutMs = 2000U;
constexpr std::uint32_t kRecoveryRetryMs = 5000U;
/* Match ref/ on the STM32N6570-DK.  Keep the CSI PHY configuration
 * unchanged; this only reduces the sensor frame rate to give the CSI link
 * the same timing margin as the reference application. */
constexpr std::int32_t kFrameRateFps = 20;

#ifndef AI_IMX335_TEST_PATTERN_MODE
#define AI_IMX335_TEST_PATTERN_MODE -1
#endif
#ifndef AI_DCMIPP_BYPASS_DOWNSIZE
#define AI_DCMIPP_BYPASS_DOWNSIZE 0
#endif
#ifndef AI_DCMIPP_DISABLE_DEMOSAIC
#define AI_DCMIPP_DISABLE_DEMOSAIC 0
#endif
#ifndef AI_DCMIPP_DEMOSAIC_LINEAR
#define AI_DCMIPP_DEMOSAIC_LINEAR 0
#endif
#ifndef AI_DCMIPP_RAW_DUMP
#define AI_DCMIPP_RAW_DUMP 0
#endif

#if AI_DCMIPP_RAW_DUMP
constexpr std::uintptr_t kRawDumpBuffer = 0x91600000UL;
constexpr std::uint32_t kRawDumpBufferBytes = 4U * 1024U * 1024U;
constexpr std::uint32_t kRawDumpWordLimit = 0x7F000U;
constexpr std::uint8_t kRawDumpSentinel = 0xA5U;
bool g_raw_dump_started = false;
bool g_raw_dump_reported = false;
std::uint32_t g_raw_dump_start_tick = 0U;
#endif

volatile std::uintptr_t g_completed_frame = 0U;
std::uintptr_t g_active_frame = 0U;
std::uintptr_t g_next_frame = 0U;
std::uintptr_t g_frame_buffer0 = 0U;
std::uintptr_t g_frame_buffer1 = 0U;
volatile std::uintptr_t g_completed_inference = 0U;
std::uintptr_t g_active_inference = 0U;
std::uintptr_t g_next_inference = 0U;
std::uintptr_t g_inference_buffer0 = 0U;
std::uintptr_t g_inference_buffer1 = 0U;
volatile std::uint32_t g_inference_sequence = 0U;
uai::ai::memory_allocator::MemoryAllocator *g_pipe2_memory = nullptr;
volatile std::uint32_t g_last_frame_tick = 0U;
volatile std::uint32_t g_last_csi_error_tick = 0U;
volatile bool g_csi_fault_pending = false;
std::uint32_t g_last_vsync_count = 0U;
std::uint32_t g_last_recovery_tick = 0U;
bool g_camera_recovery_attempted = false;

Error Hardware(const char *operation, std::uint32_t detail = 0U)
{
    return {ErrorCode::kHardware, detail, operation};
}

Error ConfigureSensor(Imx335RegisterLayer &registers)
{
    return registers.Configure(AI_IMX335_TEST_PATTERN_MODE, kFrameRateFps);
}

Error ConfigurePipe()
{
    const float ratio_width = static_cast<float>(kSensorWidth) / kOutputWidth;
    const float ratio_height = static_cast<float>(kSensorHeight) / kOutputHeight;
    const float ratio = ratio_width < ratio_height ? ratio_width : ratio_height;
    DCMIPP_CropConfTypeDef crop{};
    crop.HSize = AI_DCMIPP_BYPASS_DOWNSIZE != 0 ? kOutputWidth
        : static_cast<std::uint32_t>(kOutputWidth * ratio);
    crop.VSize = AI_DCMIPP_BYPASS_DOWNSIZE != 0 ? kOutputHeight
        : static_cast<std::uint32_t>(kOutputHeight * ratio);
    crop.HStart = ((kSensorWidth - crop.HSize) / 2U) & ~1U;
    crop.VStart = ((kSensorHeight - crop.VSize) / 2U) & ~1U;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableDecimation(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableRedBlueSwap(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableGammaConversion(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK) {
        return Hardware("camera.pipe.crop");
    }
    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = static_cast<std::uint32_t>(8192.0F * crop.HSize / kOutputWidth);
    downsize.VRatio = static_cast<std::uint32_t>(8192.0F * crop.VSize / kOutputHeight);
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = kOutputWidth;
    downsize.VSize = kOutputHeight;
    HAL_StatusTypeDef downsize_status = HAL_OK;
    if constexpr (AI_DCMIPP_BYPASS_DOWNSIZE != 0) {
        downsize_status = HAL_DCMIPP_PIPE_DisableDownsize(&hcamera_dcmipp, DCMIPP_PIPE1);
    } else {
        downsize_status = HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &downsize);
        if (downsize_status == HAL_OK) {
            downsize_status = HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, DCMIPP_PIPE1);
        }
    }
    if (downsize_status != HAL_OK) return Hardware("camera.pipe.downsize");
    DCMIPP_PipeConfTypeDef pipe{};
    pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
    pipe.PixelPipePitch = kOutputWidth * 2U;
    pipe.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    const HAL_StatusTypeDef status = HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &pipe);
    return status == HAL_OK ? Error{ErrorCode::kOk, 0U, "camera.pipe.configure"}
                            : Hardware("camera.pipe.configure", status);
}

Error ConfigureInferencePipe()
{
    /* Pipe2 is the ancillary NN output. It consumes the same RAW10 CSI
     * stream as Pipe1, then performs the crop/scale and RGB888 packing in
     * hardware. This is the same topology used by ref/. */
    DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe{};
    csi_pipe.DataTypeMode = DCMIPP_DTMODE_DTIDA;
    csi_pipe.DataTypeIDA = DCMIPP_DT_RAW10;
    csi_pipe.DataTypeIDB = DCMIPP_DT_RAW10;
    if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE2,
                                      &csi_pipe) != HAL_OK ||
        HAL_DCMIPP_PIPE_CSI_EnableShare(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK) {
        return Hardware("camera.pipe2.csi");
    }

    const float ratio_width = static_cast<float>(kSensorWidth) / kOutputWidth;
    const float ratio_height = static_cast<float>(kSensorHeight) / kOutputHeight;
    const float display_to_sensor = ratio_width < ratio_height
                                        ? ratio_width
                                        : ratio_height;
    DCMIPP_CropConfTypeDef crop{};
    /* Keep Pipe2's source geometry identical to the reference person
     * detector: the 800x480 display aspect-ratio crop is resized to the
     * model's 480x480 tensor. The model was trained with this same horizontal
     * stretch. */
    if (AI_DCMIPP_BYPASS_DOWNSIZE != 0) {
        crop.HSize = kInferenceWidth;
        crop.VSize = kInferenceHeight;
    } else {
        crop.HSize = static_cast<std::uint32_t>(
            static_cast<float>(kOutputWidth) * display_to_sensor);
        crop.VSize = static_cast<std::uint32_t>(
            static_cast<float>(kOutputHeight) * display_to_sensor);
    }
    crop.HStart = ((kSensorWidth - crop.HSize) / 2U) & ~1U;
    crop.VStart = ((kSensorHeight - crop.VSize) / 2U) & ~1U;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE2, &crop) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE2) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableDecimation(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK) {
        return Hardware("camera.pipe2.crop");
    }

    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = static_cast<std::uint32_t>(
        8192.0F * crop.HSize / kInferenceWidth);
    downsize.VRatio = static_cast<std::uint32_t>(
        8192.0F * crop.VSize / kInferenceHeight);
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = kInferenceWidth;
    downsize.VSize = kInferenceHeight;
    if (HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, DCMIPP_PIPE2,
                                          &downsize) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK) {
        return Hardware("camera.pipe2.downsize");
    }

    DCMIPP_PipeConfTypeDef pipe{};
    /* Pipe2 is consumed by the NPU once every few seconds.  Capturing every
     * CSI frame needlessly adds a 480x480 RGB888 write to external PSRAM at
     * 20 fps and can starve Pipe1 while the NPU reads the same memory bus. */
    pipe.FrameRate = DCMIPP_FRAME_RATE_1_OVER_4;
    pipe.PixelPipePitch = kInferenceWidth * 3U;
    pipe.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
    if (HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE2, &pipe) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_EnableRedBlueSwap(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_DisableGammaConversion(&hcamera_dcmipp, DCMIPP_PIPE2) !=
            HAL_OK) {
        return Hardware("camera.pipe2.configure");
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "camera: pipe2 input crop x=%u y=%u w=%u h=%u output=%ux%u\n"),
              static_cast<unsigned int>(crop.HStart),
              static_cast<unsigned int>(crop.VStart),
              static_cast<unsigned int>(crop.HSize),
              static_cast<unsigned int>(crop.VSize),
              static_cast<unsigned int>(kInferenceWidth),
              static_cast<unsigned int>(kInferenceHeight));
    return {ErrorCode::kOk, 0U, "camera.pipe2.configure"};
}

Error ConfigureRawDumpPipe()
{
#if AI_DCMIPP_RAW_DUMP
    DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe{};
    csi_pipe.DataTypeMode = DCMIPP_DTMODE_DTIDA;
    csi_pipe.DataTypeIDA = DCMIPP_DT_RAW10;
    csi_pipe.DataTypeIDB = DCMIPP_DT_RAW10;
    if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE0, &csi_pipe) != HAL_OK) {
        return Hardware("camera.raw.configure");
    }
    DCMIPP_PipeConfTypeDef pipe{};
    pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
    if (HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE0, &pipe) != HAL_OK) {
        return Hardware("camera.raw.configure");
    }
    DCMIPP_CropConfTypeDef crop{};
    crop.HStart = (((kSensorWidth - kOutputWidth) / 2U) * 10U) / 32U;
    crop.HSize = (kOutputWidth * 10U) / 32U;
    crop.VStart = ((kSensorHeight - kOutputHeight) / 2U) & ~1U;
    crop.VSize = kOutputHeight;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE0, &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE0) != HAL_OK) {
        return Hardware("camera.raw.crop");
    }
    WRITE_REG(hcamera_dcmipp.Instance->P0DCLMTR,
              DCMIPP_P0DCLMTR_ENABLE | kRawDumpWordLimit);
#endif
    return {ErrorCode::kOk, 0U, "camera.raw.configure"};
}

void PrepareRawDump()
{
#if AI_DCMIPP_RAW_DUMP
    g_raw_dump_started = false;
    g_raw_dump_reported = false;
    std::memset(reinterpret_cast<void *>(kRawDumpBuffer), kRawDumpSentinel,
                kRawDumpBufferBytes);
    SCB_CleanInvalidateDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(kRawDumpBuffer),
        static_cast<std::int32_t>(kRawDumpBufferBytes));
#endif
}

void StartRawDump()
{
#if AI_DCMIPP_RAW_DUMP
    __HAL_DCMIPP_CLEAR_FLAG(&hcamera_dcmipp, DCMIPP_FLAG_PIPE0_FRAME |
                                                 DCMIPP_FLAG_PIPE0_VSYNC |
                                                 DCMIPP_FLAG_PIPE0_OVR |
                                                 DCMIPP_FLAG_PIPE0_LIMIT);
    if (HAL_DCMIPP_CSI_PIPE_Start(&hcamera_dcmipp, DCMIPP_PIPE0,
                                  DCMIPP_VIRTUAL_CHANNEL0,
                                  static_cast<std::uint32_t>(kRawDumpBuffer),
                                  DCMIPP_MODE_SNAPSHOT) != HAL_OK) {
        g_raw_dump_reported = true;
        return;
    }
    __HAL_DCMIPP_DISABLE_IT(&hcamera_dcmipp, DCMIPP_IT_PIPE0_FRAME |
                                                   DCMIPP_IT_PIPE0_VSYNC |
                                                   DCMIPP_IT_PIPE0_OVR);
    g_raw_dump_started = true;
    g_raw_dump_start_tick = HAL_GetTick();
#endif
}

void ProcessRawDump()
{
#if AI_DCMIPP_RAW_DUMP
    if (!g_raw_dump_started || g_raw_dump_reported) return;
    const std::uint32_t flags = READ_REG(hcamera_dcmipp.Instance->CMSR2);
    if ((flags & DCMIPP_FLAG_PIPE0_FRAME) != 0U) {
        __HAL_DCMIPP_CLEAR_FLAG(&hcamera_dcmipp, DCMIPP_FLAG_PIPE0_FRAME);
        g_raw_dump_reported = true;
    } else if (HAL_GetTick() - g_raw_dump_start_tick >= 1000U) {
        g_raw_dump_reported = true;
    }
#endif
}

Error ApplyDemosaicDiagnostic()
{
#if AI_DCMIPP_DEMOSAIC_LINEAR
    DCMIPP_RawBayer2RGBConfTypeDef config{};
    config.RawBayerType = DCMIPP_RAWBAYER_RGGB;
    HAL_StatusTypeDef status = HAL_DCMIPP_PIPE_SetISPRawBayer2RGBConfig(&hcamera_dcmipp, DCMIPP_PIPE1, &config);
    if (status == HAL_OK) status = HAL_DCMIPP_PIPE_EnableISPRawBayer2RGB(&hcamera_dcmipp, DCMIPP_PIPE1);
    return status == HAL_OK ? Error{ErrorCode::kOk, 0U, "camera.demosaic"} : Hardware("camera.demosaic", status);
#elif AI_DCMIPP_DISABLE_DEMOSAIC
    const HAL_StatusTypeDef status = HAL_DCMIPP_PIPE_DisableISPRawBayer2RGB(&hcamera_dcmipp, DCMIPP_PIPE1);
    return status == HAL_OK ? Error{ErrorCode::kOk, 0U, "camera.demosaic"} : Hardware("camera.demosaic", status);
#else
    return {ErrorCode::kOk, 0U, "camera.demosaic"};
#endif
}

Error StartStream(const Imx335RegisterLayer &registers)
{
    return registers.SetStreaming(true);
}

} // namespace

extern "C" {
volatile unsigned int g_camera_vsync_event_count = 0U;
volatile unsigned int g_camera_frame_event_count = 0U;
volatile unsigned int g_camera_recovery_count = 0U;
volatile unsigned int g_camera_recovery_error_count = 0U;
volatile unsigned int g_camera_isp_error_count = 0U;
volatile unsigned int g_camera_dcmipp_last_status = 0U;
volatile unsigned int g_camera_dcmipp_error_count = 0U;
volatile unsigned int g_camera_camera_error_count = 0U;
volatile unsigned int g_camera_pipe2_frame_event_count = 0U;
volatile unsigned int g_camera_pipe2_drop_count = 0U;
volatile unsigned int g_camera_csi_last_status = 0U;
volatile unsigned int g_camera_csi_last_status1 = 0U;
volatile unsigned int g_camera_csi_last_pending_status = 0U;
volatile unsigned int g_camera_csi_last_pending_status1 = 0U;
volatile unsigned int g_camera_csi_error_count = 0U;
volatile unsigned int g_camera_csi_last_error_code = 0U;
volatile unsigned int g_camera_csi_sot_sync_dl0_count = 0U;
volatile unsigned int g_camera_csi_sot_sync_dl1_count = 0U;
volatile unsigned int g_camera_csi_sot_dl0_count = 0U;
volatile unsigned int g_camera_csi_sot_dl1_count = 0U;
}

namespace uai::ai::camera {

using common::Error;
using common::ErrorCode;

namespace {

void LogCameraLinkState(
    const sensor::registers::Imx335RegisterLayer &sensor_registers)
{
    std::uint8_t mode = 0xFFU;
    const bool sensor_read_ok = sensor_registers.Read(
        sensor::registers::Imx335Register::kModeSelect, &mode,
        sizeof(mode)).Ok();
    const std::uint32_t dcmipp_status1 = hcamera_dcmipp.Instance->CMSR1;
    const std::uint32_t dcmipp_status2 = hcamera_dcmipp.Instance->CMSR2;
    const std::uint32_t pipe_status = hcamera_dcmipp.Instance->P1SR;

    tm_printf(reinterpret_cast<const UB *>(
                  "camera: diag t=%u f=%u v=%u imx=%x/%u "
                  "csi=%x/%x dcmipp=%x/%x p1=%x\n"),
              static_cast<unsigned int>(HAL_GetTick()),
              g_camera_frame_event_count, g_camera_vsync_event_count,
              static_cast<unsigned int>(mode),
              static_cast<unsigned int>(sensor_read_ok), CSI->SR0, CSI->SR1,
              dcmipp_status1, dcmipp_status2, pipe_status);
}

} // namespace

void InstallExposureWorkaround()
{
    hcamera_isp.appliHelpers.SetSensorExposure = AiSetImx335Exposure;
    hcamera_isp.appliHelpers.GetSensorExposure = AiGetImx335Exposure;
    hcamera_isp.appliHelpers.SetSensorGain = AiSetImx335Gain;
    hcamera_isp.appliHelpers.GetSensorGain = AiGetImx335Gain;
    AiResetImx335ControlState();
}

Error CameraDriver::Initialize(memory_allocator::MemoryAllocator &memory,
                                cache::CacheDriver &cache)
{
    if (initialized_) return {ErrorCode::kAlreadyInitialized, 0U, "camera.initialize"};
    std::uintptr_t first = 0U, second = 0U;
    if (!memory.CaptureBuffers(&first, &second).Ok()) return {ErrorCode::kNotInitialized, 0U, "camera.initialize"};
    if (BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) != BSP_ERROR_NONE) return Hardware("camera.initialize");
    Imx335RegisterLayer registers;
    if (!ConfigureSensor(registers).Ok()) return Hardware("camera.sensor.configure");
    InstallExposureWorkaround();
    if (!ConfigurePipe().Ok() || !ConfigureInferencePipe().Ok() ||
        !ConfigureRawDumpPipe().Ok()) {
        return Hardware("camera.configure");
    }
    memory_ = &memory;
    cache_ = &cache;
    g_pipe2_memory = &memory;
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "camera.initialize"};
}

void CameraDriver::KeepClocksOnSleep() const
{
    __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();
    __HAL_RCC_CSI_CLK_SLEEP_ENABLE();
}

Error CameraDriver::Start()
{
    if (!initialized_ || memory_ == nullptr || cache_ == nullptr) return {ErrorCode::kNotInitialized, 0U, "camera.start"};
    if (started_) return {ErrorCode::kAlreadyInitialized, 0U, "camera.start"};
    std::uintptr_t first = 0U, second = 0U;
    Error status = memory_->CaptureBuffers(&first, &second);
    if (!status.Ok()) return status;
    const memory_allocator::Buffer first_buffer{first, memory_allocator::kFrameBytes, 0U, memory_allocator::Region::kCapture};
    const memory_allocator::Buffer second_buffer{second, memory_allocator::kFrameBytes, 1U, memory_allocator::Region::kCapture};
    status = cache_->PrepareForDmaWrite(first_buffer);
    if (!status.Ok()) return status;
    status = cache_->PrepareForDmaWrite(second_buffer);
    if (!status.Ok()) return status;
    std::uintptr_t inference_first = 0U, inference_second = 0U;
    status = memory_->InferenceBuffers(&inference_first, &inference_second);
    if (!status.Ok()) return status;
    const memory_allocator::Buffer inference_first_buffer{
        inference_first, memory_allocator::kInferenceBufferBytes, 0U,
        memory_allocator::Region::kInference};
    const memory_allocator::Buffer inference_second_buffer{
        inference_second, memory_allocator::kInferenceBufferBytes, 1U,
        memory_allocator::Region::kInference};
    status = cache_->PrepareForDmaWrite(inference_first_buffer);
    if (!status.Ok()) return status;
    status = cache_->PrepareForDmaWrite(inference_second_buffer);
    if (!status.Ok()) return status;
    g_frame_buffer0 = first; g_frame_buffer1 = second;
    g_active_frame = first; g_next_frame = second; g_completed_frame = 0U;
    g_inference_buffer0 = inference_first;
    g_inference_buffer1 = inference_second;
    g_active_inference = inference_first;
    g_next_inference = inference_second;
    g_completed_inference = 0U;
    g_inference_sequence = 0U;
    g_csi_fault_pending = false;
    g_camera_recovery_attempted = false;
    PrepareRawDump();
    if (BSP_CAMERA_Start(0U, reinterpret_cast<uint8_t *>(g_active_frame), CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) return Hardware("camera.start");
    if (HAL_DCMIPP_CSI_PIPE_Start(&hcamera_dcmipp, DCMIPP_PIPE2,
                                  DCMIPP_VIRTUAL_CHANNEL0,
                                  static_cast<std::uint32_t>(g_active_inference),
                                  DCMIPP_MODE_CONTINUOUS) != HAL_OK) {
        return Hardware("camera.pipe2.start");
    }
    status = ApplyDemosaicDiagnostic();
    if (!status.Ok()) return status;
    Imx335RegisterLayer registers;
    status = StartStream(registers);
    if (!status.Ok()) return status;
    StartRawDump();
    g_last_frame_tick = HAL_GetTick();
    g_last_vsync_count = g_camera_vsync_event_count;
    started_ = true;
    LogCameraLinkState(registers);
    return {ErrorCode::kOk, 0U, "camera.start"};
}

Error CameraDriver::Stop()
{
    if (!initialized_ || !started_) return {ErrorCode::kNotInitialized, 0U, "camera.stop"};
    Imx335RegisterLayer registers;
    const auto standby = registers.SetStreaming(false);
    if (!standby.Ok()) return standby;
    if (HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2,
                                 DCMIPP_VIRTUAL_CHANNEL0) != HAL_OK) {
        return Hardware("camera.pipe2.stop");
    }
    if (BSP_CAMERA_Stop(0U) != BSP_ERROR_NONE) return Hardware("camera.stop");
    started_ = false;
    return {ErrorCode::kOk, 0U, "camera.stop"};
}

Error CameraDriver::Process()
{
    if (!initialized_ || !started_) return {ErrorCode::kNotInitialized, 0U, "camera.process"};
    ProcessRawDump();
    if (g_camera_vsync_event_count != g_last_vsync_count) {
        g_last_vsync_count = g_camera_vsync_event_count;
        if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE) ++g_camera_isp_error_count;
    }
    const std::uint32_t now = HAL_GetTick();
    const bool timed_out = now - g_last_frame_tick >= kFrameTimeoutMs;
    /* A CSI error is diagnostic information, not by itself a reason to
     * restart the sensor.  The reference application waits for an actual
     * frame timeout; doing the same avoids restarting on a transient SOT or
     * D-PHY status bit while frames are still arriving. */
    if (timed_out &&
        (!g_camera_recovery_attempted ||
         now - g_last_recovery_tick >= kRecoveryRetryMs)) {
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: no frame for %u ms; starting recovery #%u\n"),
                  static_cast<unsigned int>(now - g_last_frame_tick),
                  g_camera_recovery_count + 1U);
        Imx335RegisterLayer registers;
        LogCameraLinkState(registers);
        g_last_recovery_tick = now;
        g_camera_recovery_attempted = true;
        ++g_camera_recovery_count;
        g_completed_frame = 0U;
        g_active_frame = g_frame_buffer0;
        g_next_frame = g_frame_buffer1;
        g_completed_inference = 0U;
        g_active_inference = g_inference_buffer0;
        g_next_inference = g_inference_buffer1;
        (void)registers.SetStreaming(false);
        HAL_Delay(20U);
        const HAL_StatusTypeDef pipe_stop_status =
            HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE1,
                                     DCMIPP_VIRTUAL_CHANNEL0);
        const HAL_StatusTypeDef pipe2_stop_status =
            HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2,
                                     DCMIPP_VIRTUAL_CHANNEL0);
        bool recovery_ok = pipe_stop_status == HAL_OK &&
            pipe2_stop_status == HAL_OK &&
            HAL_DCMIPP_DeInit(&hcamera_dcmipp) == HAL_OK &&
            BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) == BSP_ERROR_NONE &&
            ConfigureSensor(registers).Ok();
        if (recovery_ok) {
            InstallExposureWorkaround();
            recovery_ok = ConfigurePipe().Ok() && ConfigureInferencePipe().Ok() &&
                          ConfigureRawDumpPipe().Ok();
        }
        if (!recovery_ok ||
            BSP_CAMERA_Start(0U, reinterpret_cast<uint8_t *>(g_active_frame), CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE ||
            HAL_DCMIPP_CSI_PIPE_Start(
                &hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0,
                static_cast<std::uint32_t>(g_active_inference),
                DCMIPP_MODE_CONTINUOUS) != HAL_OK ||
            !ApplyDemosaicDiagnostic().Ok() || !StartStream(registers).Ok()) {
            ++g_camera_recovery_error_count;
            LogCameraLinkState(registers);
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: recovery failed attempts=%u failed=%u\n"),
                      g_camera_recovery_count, g_camera_recovery_error_count);
        } else {
            InstallExposureWorkaround();
            PrepareRawDump();
            StartRawDump();
            g_csi_fault_pending = false;
            g_last_frame_tick = now;
            g_last_vsync_count = g_camera_vsync_event_count;
            LogCameraLinkState(registers);
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: recovery done attempts=%u frames=%u\n"),
                      g_camera_recovery_count, g_camera_frame_event_count);
        }
    }
    return {ErrorCode::kOk, 0U, "camera.process"};
}

Error CameraDriver::TakeCompletedCapture(memory_allocator::CaptureFrame *frame)
{
    if (!initialized_ || memory_ == nullptr) return {ErrorCode::kNotInitialized, 0U, "camera.take_capture"};
    if (frame == nullptr) return {ErrorCode::kInvalidArgument, 0U, "camera.take_capture"};
    const std::uintptr_t address = g_completed_frame;
    g_completed_frame = 0U;
    if (address == 0U) return {ErrorCode::kNoFrame, 0U, "camera.take_capture"};
    return memory_->ImportCompletedCapture(address, frame);
}

Error CameraDriver::TakeCompletedInference(
    memory_allocator::InferenceFrame *frame)
{
    if (!initialized_ || memory_ == nullptr) {
        return {ErrorCode::kNotInitialized, 0U, "camera.take_inference"};
    }
    if (frame == nullptr) {
        return {ErrorCode::kInvalidArgument, 0U, "camera.take_inference"};
    }
    const std::uintptr_t address = g_completed_inference;
    const std::uint32_t sequence = g_inference_sequence;
    g_completed_inference = 0U;
    if (address == 0U) {
        return {ErrorCode::kNoFrame, 0U, "camera.take_inference"};
    }
    return memory_->ImportCompletedInference(address, sequence, frame);
}

} // namespace uai::ai::camera

extern "C" void BSP_CAMERA_FrameEventCallback(uint32_t Instance)
{
    (void)Instance;
    ++g_camera_frame_event_count;
    if (HAL_DCMIPP_PIPE_SetMemoryAddress(&hcamera_dcmipp, DCMIPP_PIPE1, DCMIPP_MEMORY_ADDRESS_0, static_cast<uint32_t>(g_next_frame)) != HAL_OK) {
        ++g_camera_dcmipp_error_count;
        return;
    }
    g_last_frame_tick = HAL_GetTick();
    g_csi_fault_pending = false;
    g_camera_recovery_attempted = false;
    g_completed_frame = g_active_frame;
    g_active_frame = g_next_frame;
    g_next_frame = g_next_frame == g_frame_buffer0 ? g_frame_buffer1 : g_frame_buffer0;
}

extern "C" void BSP_CAMERA_VsyncEventCallback(uint32_t Instance) { (void)Instance; ++g_camera_vsync_event_count; }
extern "C" void BSP_CAMERA_PipeErrorCallback(uint32_t Instance) { (void)Instance; ++g_camera_dcmipp_error_count; }
extern "C" void BSP_CAMERA_ErrorCallback(uint32_t Instance) { (void)Instance; ++g_camera_camera_error_count; }

extern "C" void AiCameraPipe2FrameEventCallback(void)
{
    ++g_camera_pipe2_frame_event_count;
    const std::uintptr_t completed = g_active_inference;
    g_completed_inference = completed;
    const std::uint32_t sequence = ++g_inference_sequence;
    const std::uintptr_t other = g_next_inference;
    std::uintptr_t selected = 0U;
    if (g_pipe2_memory != nullptr &&
        g_pipe2_memory->IsInferenceBufferFree(other)) {
        selected = other;
    } else if (g_pipe2_memory != nullptr &&
               g_pipe2_memory->IsInferenceBufferFree(completed)) {
        /* If NPU owns the other slot, keep the just-completed slot as a
         * drop/reuse slot. Pipe1 remains independent and continues to render. */
        selected = completed;
        ++g_camera_pipe2_drop_count;
    } else {
        ++g_camera_pipe2_drop_count;
        return;
    }
    if (HAL_DCMIPP_PIPE_SetMemoryAddress(
            &hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_MEMORY_ADDRESS_0,
            static_cast<std::uint32_t>(selected)) != HAL_OK) {
        ++g_camera_dcmipp_error_count;
        return;
    }
    (void)sequence;
    g_active_inference = selected;
    g_next_inference = selected == g_inference_buffer0 ? g_inference_buffer1
                                                       : g_inference_buffer0;
}

extern "C" void DCMIPP_IRQHandler(void)
{
    g_camera_dcmipp_last_status = hcamera_dcmipp.Instance->CMSR2;
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}

extern "C" void CSI_IRQHandler(void)
{
    const uint32_t status0 = CSI->SR0, status1 = CSI->SR1;
    constexpr uint32_t errors0 =
        DCMIPP_CSI_IT_SYNCERR | DCMIPP_CSI_IT_WDERR |
        DCMIPP_CSI_IT_SPKTERR | DCMIPP_CSI_IT_IDERR |
        DCMIPP_CSI_IT_CECCERR | DCMIPP_CSI_IT_ECCERR |
        DCMIPP_CSI_IT_CRCERR | DCMIPP_CSI_IT_CCFIFO;
    constexpr uint32_t errors1 =
        DCMIPP_CSI_IT_ECTRLDL1 | DCMIPP_CSI_IT_ESYNCESCDL1 |
        DCMIPP_CSI_IT_EESCDL1 | DCMIPP_CSI_IT_ESOTSYNCDL1 |
        DCMIPP_CSI_IT_ESOTDL1 | DCMIPP_CSI_IT_ECTRLDL0 |
        DCMIPP_CSI_IT_ESYNCESCDL0 | DCMIPP_CSI_IT_EESCDL0 |
        DCMIPP_CSI_IT_ESOTSYNCDL0 | DCMIPP_CSI_IT_ESOTDL0;
    const uint32_t pending0 = status0 & CSI->IER0 & errors0;
    const uint32_t pending1 = status1 & CSI->IER1 & errors1;
    if ((pending1 & DCMIPP_CSI_IT_ESOTSYNCDL0) != 0U) ++g_camera_csi_sot_sync_dl0_count;
    if ((pending1 & DCMIPP_CSI_IT_ESOTSYNCDL1) != 0U) ++g_camera_csi_sot_sync_dl1_count;
    if ((pending1 & DCMIPP_CSI_IT_ESOTDL0) != 0U) ++g_camera_csi_sot_dl0_count;
    if ((pending1 & DCMIPP_CSI_IT_ESOTDL1) != 0U) ++g_camera_csi_sot_dl1_count;
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);
    if (pending0 != 0U || pending1 != 0U) {
        g_camera_csi_last_status = status0;
        g_camera_csi_last_status1 = status1;
        g_camera_csi_last_pending_status = pending0;
        g_camera_csi_last_pending_status1 = pending1;
        g_camera_csi_last_error_code = hcamera_dcmipp.ErrorCode;
        ++g_camera_csi_error_count;
        g_last_csi_error_tick = HAL_GetTick();
        g_csi_fault_pending = true;
        // HAL_DCMIPP_CSI_IRQHandler() disables and clears the faulty CSI
        // interrupt source.  Do not re-arm it here: repeated D-PHY errors
        // must not turn into an interrupt storm.  The normal frame watchdog
        // and recovery path remain enabled as the fail-safe.
    }
}

extern "C" HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
    UNUSED(hdcmipp);
    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
    clock.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
    clock.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC17].ClockDivider = 4U;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return HAL_ERROR;
    clock.PeriphClockSelection = RCC_PERIPHCLK_CSI;
    clock.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC18].ClockDivider = 60U;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}
