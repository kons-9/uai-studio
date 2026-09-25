#include "driver/camera/usecase/camera_use_case.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
#include "imx335.h"
#include "stm32n6570_discovery_camera.h"
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern ISP_HandleTypeDef hcamera_isp;
extern void *Camera_CompObj;
ISP_StatusTypeDef AiSetImx335Exposure(uint32_t instance, int32_t exposure);
ISP_StatusTypeDef AiGetImx335Exposure(uint32_t instance, int32_t *exposure);
ISP_StatusTypeDef AiSetImx335Gain(uint32_t instance, int32_t gain_mdB);
ISP_StatusTypeDef AiGetImx335Gain(uint32_t instance, int32_t *gain_mdB);
}

namespace {

using uai::ai::common::Error;
using uai::ai::common::ErrorCode;
using uai::ai::camera::registers::Imx335RegisterLayer;

constexpr std::uint32_t kSensorWidth = 2592U;
constexpr std::uint32_t kSensorHeight = 1944U;
constexpr std::uint32_t kOutputWidth = 800U;
constexpr std::uint32_t kOutputHeight = 480U;
constexpr std::uint32_t kFrameTimeoutMs = 2000U;
constexpr std::uint32_t kCsiFaultTimeoutMs = 100U;
constexpr std::uint32_t kRecoveryRetryMs = 5000U;
constexpr std::int32_t kFrameRateFps = 30;
constexpr std::uint32_t kVmaxAt30Fps = 4500U;
constexpr std::uint32_t kMinimumShutterLines = 9U;

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

Error ConfigureSensor()
{
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr ||
        IMX335_SetTestPattern(sensor, AI_IMX335_TEST_PATTERN_MODE) != IMX335_OK ||
        IMX335_SetFramerate(sensor, kFrameRateFps) != IMX335_OK) {
        return Hardware("camera.sensor.configure");
    }
    /* Keep the sensor's stock IMX335 timing profile. */
    return {ErrorCode::kOk, 0U, "camera.sensor.configure"};
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
volatile unsigned int g_ai_last_exposure_request_us = 0U;
volatile unsigned int g_ai_last_exposure_lines = 0U;
volatile unsigned int g_ai_last_sensor_gain_mdB = 0U;
}

namespace uai::ai::camera::usecase {

using common::Error;
using common::ErrorCode;

void InstallExposureWorkaround()
{
    hcamera_isp.appliHelpers.SetSensorExposure = AiSetImx335Exposure;
    hcamera_isp.appliHelpers.GetSensorExposure = AiGetImx335Exposure;
    hcamera_isp.appliHelpers.SetSensorGain = AiSetImx335Gain;
    hcamera_isp.appliHelpers.GetSensorGain = AiGetImx335Gain;
    g_ai_last_exposure_request_us = 0U;
    g_ai_last_exposure_lines = 0U;
    g_ai_last_sensor_gain_mdB = IMX335_GAIN_MIN;
}

Error CameraUseCase::Initialize(memory_manager::MemoryManager &memory,
                                memory_manager::MemoryHardware &memory_hardware)
{
    if (initialized_) return {ErrorCode::kAlreadyInitialized, 0U, "camera.initialize"};
    std::uintptr_t first = 0U, second = 0U;
    if (!memory.CaptureBuffers(&first, &second).Ok()) return {ErrorCode::kNotInitialized, 0U, "camera.initialize"};
    if (BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) != BSP_ERROR_NONE) return Hardware("camera.initialize");
    if (!ConfigureSensor().Ok()) return Hardware("camera.sensor.configure");
    InstallExposureWorkaround();
    if (!ConfigurePipe().Ok() || !ConfigureRawDumpPipe().Ok()) {
        return Hardware("camera.configure");
    }
    memory_ = &memory;
    memory_hardware_ = &memory_hardware;
    initialized_ = true;
    return {ErrorCode::kOk, 0U, "camera.initialize"};
}

Error CameraUseCase::Start()
{
    if (!initialized_ || memory_ == nullptr || memory_hardware_ == nullptr) return {ErrorCode::kNotInitialized, 0U, "camera.start"};
    if (started_) return {ErrorCode::kAlreadyInitialized, 0U, "camera.start"};
    std::uintptr_t first = 0U, second = 0U;
    Error status = memory_->CaptureBuffers(&first, &second);
    if (!status.Ok()) return status;
    const memory_manager::Buffer first_buffer{first, memory_manager::kFrameBytes, 0U, memory_manager::Region::kCapture};
    const memory_manager::Buffer second_buffer{second, memory_manager::kFrameBytes, 1U, memory_manager::Region::kCapture};
    status = memory_hardware_->PrepareForDmaWrite(first_buffer);
    if (!status.Ok()) return status;
    status = memory_hardware_->PrepareForDmaWrite(second_buffer);
    if (!status.Ok()) return status;
    g_frame_buffer0 = first; g_frame_buffer1 = second; g_active_frame = first; g_next_frame = second; g_completed_frame = 0U; g_csi_fault_pending = false; g_camera_recovery_attempted = false;
    PrepareRawDump();
    if (BSP_CAMERA_Start(0U, reinterpret_cast<uint8_t *>(g_active_frame), CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) return Hardware("camera.start");
    status = ApplyDemosaicDiagnostic();
    if (!status.Ok()) return status;
    status = StartStream(registers_);
    if (!status.Ok()) return status;
    StartRawDump();
    g_last_frame_tick = HAL_GetTick();
    g_last_vsync_count = g_camera_vsync_event_count;
    started_ = true;
    return {ErrorCode::kOk, 0U, "camera.start"};
}

Error CameraUseCase::Stop()
{
    if (!initialized_ || !started_) return {ErrorCode::kNotInitialized, 0U, "camera.stop"};
    const auto standby = registers_.SetStreaming(false);
    if (!standby.Ok()) return standby;
    if (BSP_CAMERA_Stop(0U) != BSP_ERROR_NONE) return Hardware("camera.stop");
    started_ = false;
    return {ErrorCode::kOk, 0U, "camera.stop"};
}

Error CameraUseCase::Process()
{
    if (!initialized_ || !started_) return {ErrorCode::kNotInitialized, 0U, "camera.process"};
    ProcessRawDump();
    if (g_camera_vsync_event_count != g_last_vsync_count) {
        g_last_vsync_count = g_camera_vsync_event_count;
        if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE) ++g_camera_isp_error_count;
    }
    const std::uint32_t now = HAL_GetTick();
    const bool timed_out = now - g_last_frame_tick >= kFrameTimeoutMs;
    const bool csi_stalled = g_csi_fault_pending && now - g_last_csi_error_tick >= kCsiFaultTimeoutMs;
    if ((timed_out || csi_stalled) &&
        (!g_camera_recovery_attempted ||
         now - g_last_recovery_tick >= kRecoveryRetryMs)) {
        tm_printf(reinterpret_cast<const UB *>(
                      "camera: no frame for %u ms csi_fault=%u; starting recovery #%u\n"),
                  static_cast<unsigned int>(now - g_last_frame_tick),
                  static_cast<unsigned int>(csi_stalled),
                  g_camera_recovery_count + 1U);
        g_last_recovery_tick = now;
        g_camera_recovery_attempted = true;
        ++g_camera_recovery_count;
        g_completed_frame = 0U;
        g_active_frame = g_frame_buffer0;
        g_next_frame = g_frame_buffer1;
        (void)BSP_CAMERA_Stop(0U);
        (void)HAL_DCMIPP_DeInit(&hcamera_dcmipp);
        bool recovery_ok =
            BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) == BSP_ERROR_NONE &&
            ConfigureSensor().Ok();
        if (recovery_ok) {
            InstallExposureWorkaround();
            recovery_ok = ConfigurePipe().Ok() && ConfigureRawDumpPipe().Ok();
        }
        if (!recovery_ok ||
            BSP_CAMERA_Start(0U, reinterpret_cast<uint8_t *>(g_active_frame), CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE ||
            !ApplyDemosaicDiagnostic().Ok() || !StartStream(registers_).Ok()) {
            ++g_camera_recovery_error_count;
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
            tm_printf(reinterpret_cast<const UB *>(
                          "camera: recovery done attempts=%u frames=%u\n"),
                      g_camera_recovery_count, g_camera_frame_event_count);
        }
    }
    return {ErrorCode::kOk, 0U, "camera.process"};
}

Error CameraUseCase::TakeCompletedCapture(memory_manager::CaptureFrame *frame)
{
    if (!initialized_ || memory_ == nullptr) return {ErrorCode::kNotInitialized, 0U, "camera.take_capture"};
    if (frame == nullptr) return {ErrorCode::kInvalidArgument, 0U, "camera.take_capture"};
    const std::uintptr_t address = g_completed_frame;
    g_completed_frame = 0U;
    if (address == 0U) return {ErrorCode::kNoFrame, 0U, "camera.take_capture"};
    return memory_->ImportCompletedCapture(address, frame);
}

Error CameraUseCase::ReadSensorRegisters(registers::SensorRegisterSnapshot *snapshot) const
{
    if (!initialized_) return {ErrorCode::kNotInitialized, 0U, "camera.read_sensor_registers"};
    return registers_.ReadSnapshot(snapshot);
}

} // namespace uai::ai::camera::usecase

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
        CSI->IER0 |= pending0;
        CSI->IER1 |= pending1;
    }
}

extern "C" HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
    UNUSED(hdcmipp);
    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
    clock.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
    clock.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
    clock.ICSelection[RCC_IC17].ClockDivider = 3U;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return HAL_ERROR;
    clock.PeriphClockSelection = RCC_PERIPHCLK_CSI;
    clock.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC18].ClockDivider = 40U;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}

extern "C" ISP_StatusTypeDef AiSetImx335Exposure(uint32_t instance, int32_t exposure)
{
    (void)instance;
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr || exposure < 0) return ISP_ERR_EINVAL;
    g_ai_last_exposure_request_us = static_cast<unsigned int>(exposure);
    uint32_t lines = static_cast<uint32_t>(static_cast<float>(exposure) / (1000000.0F / (4500U * 30U)));
    if (lines > kVmaxAt30Fps - kMinimumShutterLines) lines = kVmaxAt30Fps - kMinimumShutterLines;
    g_ai_last_exposure_lines = lines;
    return IMX335_SetExposure(sensor, static_cast<int32_t>(lines * 7U)) == IMX335_OK ? ISP_OK : ISP_ERR_EINVAL;
}
extern "C" ISP_StatusTypeDef AiGetImx335Exposure(uint32_t instance, int32_t *exposure)
{
    (void)instance;
    if (exposure == nullptr || Camera_CompObj == nullptr) return ISP_ERR_EINVAL;
    *exposure = static_cast<int32_t>(g_ai_last_exposure_request_us);
    return ISP_OK;
}
extern "C" ISP_StatusTypeDef AiSetImx335Gain(uint32_t instance, int32_t gain_mdB)
{
    (void)instance;
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr || gain_mdB < IMX335_GAIN_MIN) return ISP_ERR_EINVAL;
    if (gain_mdB > 30000) gain_mdB = 30000;
    g_ai_last_sensor_gain_mdB = static_cast<unsigned int>(gain_mdB);
    return IMX335_SetGain(sensor, gain_mdB) == IMX335_OK ? ISP_OK : ISP_ERR_EINVAL;
}
extern "C" ISP_StatusTypeDef AiGetImx335Gain(uint32_t instance, int32_t *gain_mdB)
{
    (void)instance;
    if (gain_mdB == nullptr || Camera_CompObj == nullptr) return ISP_ERR_EINVAL;
    *gain_mdB = static_cast<int32_t>(g_ai_last_sensor_gain_mdB);
    return ISP_OK;
}
extern "C" int32_t AiGetSensorGainMdB() { return static_cast<int32_t>(g_ai_last_sensor_gain_mdB); }

extern "C" int32_t AiReadSensorRegisters(uint32_t *vmax, uint32_t *shutter, uint32_t *gain)
{
    if (Camera_CompObj == nullptr || vmax == nullptr || shutter == nullptr || gain == nullptr) return -1;
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    uint8_t raw_vmax[3] = {}, raw_shutter[3] = {}, raw_gain[2] = {};
    if (sensor->IO.ReadReg(sensor->IO.Address, IMX335_REG_VMAX, raw_vmax, 3U) != IMX335_OK ||
        sensor->IO.ReadReg(sensor->IO.Address, IMX335_REG_SHUTTER, raw_shutter, 3U) != IMX335_OK ||
        sensor->IO.ReadReg(sensor->IO.Address, IMX335_REG_GAIN, raw_gain, 2U) != IMX335_OK) return -1;
    *vmax = raw_vmax[0] | (static_cast<uint32_t>(raw_vmax[1]) << 8U) | (static_cast<uint32_t>(raw_vmax[2]) << 16U);
    *shutter = raw_shutter[0] | (static_cast<uint32_t>(raw_shutter[1]) << 8U) | (static_cast<uint32_t>(raw_shutter[2]) << 16U);
    *gain = raw_gain[0] | (static_cast<uint32_t>(raw_gain[1]) << 8U);
    return 0;
}
