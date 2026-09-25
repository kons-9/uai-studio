#include "driver_arch.hpp"

#include <cstdint>

/* CubeMX/BSPが生成・定義するカメラハンドルとコンポーネント情報を共有する。
 * フレーム数の実体はuserspace/sample2/src/main.cppにある。 */
extern "C" {
#include "stm32n6570_discovery_camera.h"
#include "imx335.h"
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern ISP_HandleTypeDef hcamera_isp;
extern void *Camera_CompObj;
extern volatile unsigned int g_camera_frame_event_count;
}

extern "C" {
volatile unsigned int g_camera_vsync_event_count = 0U;
volatile unsigned int g_camera_recovery_count = 0U;
volatile unsigned int g_camera_recovery_error_count = 0U;
volatile unsigned int g_camera_isp_error_count = 0U;
}
extern "C" {
volatile unsigned int g_camera_dcmipp_last_status = 0U;
volatile unsigned int g_camera_dcmipp_error_count = 0U;
volatile unsigned int g_camera_camera_error_count = 0U;
volatile unsigned int g_camera_csi_last_status = 0U;
volatile unsigned int g_camera_csi_last_status1 = 0U;
volatile unsigned int g_camera_csi_error_count = 0U;
volatile unsigned int g_camera_csi_last_error_code = 0U;
volatile unsigned int g_sample2_last_exposure_request_us = 0U;
volatile unsigned int g_sample2_last_exposure_lines = 0U;
volatile unsigned int g_sample2_last_sensor_gain_mdB = 0U;
}
/* 以下のセンサー操作関数はこのファイル内で後述の通り定義する。 */
extern "C" ISP_StatusTypeDef Sample2SetImx335Exposure(uint32_t instance,
                                                       int32_t exposure);
extern "C" ISP_StatusTypeDef Sample2GetImx335Exposure(uint32_t instance,
                                                       int32_t *exposure);
extern "C" ISP_StatusTypeDef Sample2SetImx335Gain(uint32_t instance,
                                                   int32_t gain_mdB);
extern "C" ISP_StatusTypeDef Sample2GetImx335Gain(uint32_t instance,
                                                   int32_t *gain_mdB);
extern "C" int32_t Sample2GetSensorGainMdB();
extern "C" int32_t Sample2ReadSensorRegisters(uint32_t *vmax,
                                               uint32_t *shutter,
                                               uint32_t *gain);

namespace {

volatile std::uintptr_t completed_camera_frame = 0;
std::uintptr_t camera_frame_buffer0 = 0;
std::uintptr_t camera_frame_buffer1 = 0;
volatile std::uintptr_t active_camera_frame = 0;
std::uintptr_t next_camera_frame = 0;
volatile uint32_t last_camera_frame_tick = 0U;
uint32_t last_processed_vsync_count = 0U;
uint32_t last_camera_recovery_tick = 0U;
volatile bool camera_recovery_attempted = false;

constexpr uint32_t kCameraFrameTimeoutMs = 2000U;
constexpr uint32_t kCameraRecoveryRetryMs = 5000U;
constexpr int32_t kCameraFrameRateFps = 30;
constexpr uint32_t kImx335VmaxAt30Fps = 4500U;
constexpr uint32_t kImx335MinimumShutterLines = 9U;
/* Keep low-light AE from amplifying sensor noise to the IMX335's 72 dB limit. */
constexpr int32_t kImx335MaximumGainMdB = 30000;
constexpr float kImx335LinePeriodUs =
    1000000.0F / (kImx335VmaxAt30Fps * kCameraFrameRateFps);
constexpr uint32_t kCubeImx335LinePeriodUs =
    static_cast<uint32_t>(kImx335LinePeriodUs);

uai::driver::DriverStatus ConfigureCapturePipe();

uai::driver::DriverStatus InstallImx335ExposureWorkaround()
{
    if (hcamera_isp.appliHelpers.SetSensorExposure == nullptr ||
        hcamera_isp.appliHelpers.GetSensorExposure == nullptr ||
        hcamera_isp.appliHelpers.SetSensorGain == nullptr ||
        hcamera_isp.appliHelpers.GetSensorGain == nullptr) {
        return uai::driver::DriverStatus::kHardwareError;
    }
    hcamera_isp.appliHelpers.SetSensorExposure = Sample2SetImx335Exposure;
    /* Keep the ISP's feedback value in sync with the overridden setter. The
     * BSP getter reads a private cache that its own setter normally updates. */
    hcamera_isp.appliHelpers.GetSensorExposure = Sample2GetImx335Exposure;
    hcamera_isp.appliHelpers.SetSensorGain = Sample2SetImx335Gain;
    hcamera_isp.appliHelpers.GetSensorGain = Sample2GetImx335Gain;
    /* BSP_CAMERA_Init() initializes AEC before these callbacks are replaced,
     * leaving the freshly initialized sensor at its minimum exposure/gain.
     * Do not carry pre-recovery cached values into the new ISP instance. */
    g_sample2_last_exposure_request_us = 0U;
    g_sample2_last_exposure_lines = 0U;
    g_sample2_last_sensor_gain_mdB = IMX335_GAIN_MIN;
    return uai::driver::DriverStatus::kOk;
}

uai::driver::DriverStatus ConfigureSensorFrameRate()
{
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr ||
        IMX335_SetFramerate(sensor, kCameraFrameRateFps) != IMX335_OK) {
        return uai::driver::DriverStatus::kHardwareError;
    }
    return uai::driver::DriverStatus::kOk;
}

uai::driver::DriverStatus RestartImx335Streaming()
{
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    /* Match the sensor start performed by CMW_CAMERA_Start() in ref: begin
     * streaming only after the CSI pipe and ISP are ready.  The Cube sensor
     * driver shipped with this workspace has no IMX335_Start() entry point. */
    uint8_t mode = IMX335_MODE_STREAMING;
    if (imx335_write_reg(&sensor->Ctx, IMX335_REG_MODE_SELECT, &mode, 1U) !=
        IMX335_OK) {
        return uai::driver::DriverStatus::kHardwareError;
    }
    HAL_Delay(20U);
    return uai::driver::DriverStatus::kOk;
}

uai::driver::DriverStatus RecoverCameraCapture()
{
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    /* A CSI/D-PHY fault can leave the HAL capture state BUSY while its
     * capture-active bit never clears, so BSP_CAMERA_Stop() times out.
     * Reset the complete CSI/DCMIPP + sensor + ISP path. Clear the sensor
     * component's initialized flag first: IMX335_Init otherwise skips its
     * register table after the hardware sensor reset. */
    (void)IMX335_DeInit(sensor);
    (void)ISP_DeInit(&hcamera_isp);
    if (HAL_DCMIPP_DeInit(&hcamera_dcmipp) != HAL_OK ||
        BSP_CAMERA_Init(0, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
            BSP_ERROR_NONE ||
        !uai::driver::IsOk(ConfigureSensorFrameRate()) ||
        !uai::driver::IsOk(InstallImx335ExposureWorkaround()) ||
        !uai::driver::IsOk(ConfigureCapturePipe())) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    completed_camera_frame = 0U;
    active_camera_frame = camera_frame_buffer0;
    next_camera_frame = camera_frame_buffer1;

    if (BSP_CAMERA_Start(0, reinterpret_cast<uint8_t *>(active_camera_frame),
                         CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE ||
        !uai::driver::IsOk(RestartImx335Streaming())) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    last_processed_vsync_count = g_camera_vsync_event_count;
    last_camera_frame_tick = HAL_GetTick();
    return uai::driver::DriverStatus::kOk;
}

uai::driver::DriverStatus ConfigureCapturePipe()
{
    /* The ref configures PIPE1 as an 800x480 RGB565 output.  BSP_CAMERA_Init()
     * only initializes the sensor/ISP; leaving PIPE1 at its reset dimensions
     * makes it write the full sensor frame past sample2's 768-KiB buffers. */
    constexpr uint32_t kSensorWidth = 2592U;
    constexpr uint32_t kSensorHeight = 1944U;
    constexpr uint32_t kOutputWidth = 800U;
    constexpr uint32_t kOutputHeight = 480U;

    /* Match CMW_UTILS_get_crop_config() in ref: fit the requested aspect
     * ratio inside the sensor bounds.  Choosing the larger ratio makes the
     * crop wider/taller than the sensor and wraps the unsigned start offset. */
    const float ratio_width =
        static_cast<float>(kSensorWidth) / kOutputWidth;
    const float ratio_height =
        static_cast<float>(kSensorHeight) / kOutputHeight;
    const float ratio = ratio_width < ratio_height ? ratio_width : ratio_height;
    DCMIPP_CropConfTypeDef crop{};
    crop.HSize = static_cast<uint32_t>(kOutputWidth * ratio);
    crop.VSize = static_cast<uint32_t>(kOutputHeight * ratio);
    crop.HStart = (kSensorWidth - crop.HSize + 1U) / 2U;
    crop.VStart = (kSensorHeight - crop.VSize + 1U) / 2U;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;

    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = static_cast<uint32_t>(8192.0F * crop.HSize / kOutputWidth);
    downsize.VRatio = static_cast<uint32_t>(8192.0F * crop.VSize / kOutputHeight);
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = kOutputWidth;
    downsize.VSize = kOutputHeight;

    if (HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE1,
                                      &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE1) != HAL_OK ||
        HAL_DCMIPP_PIPE_DisableDecimation(&hcamera_dcmipp, DCMIPP_PIPE1) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, DCMIPP_PIPE1,
                                          &downsize) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, DCMIPP_PIPE1) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_DisableRedBlueSwap(&hcamera_dcmipp, DCMIPP_PIPE1) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_DisableGammaConversion(&hcamera_dcmipp,
                                               DCMIPP_PIPE1) != HAL_OK) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    DCMIPP_PipeConfTypeDef pipe{};
    pipe.FrameRate = DCMIPP_FRAME_RATE_ALL;
    pipe.PixelPipePitch = kOutputWidth * 2U;
    pipe.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    HAL_StatusTypeDef status;
    if (hcamera_dcmipp.PipeState[DCMIPP_PIPE1] ==
        HAL_DCMIPP_PIPE_STATE_RESET) {
        status = HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, DCMIPP_PIPE1,
                                           &pipe);
    } else {
        status = HAL_DCMIPP_PIPE_SetPixelPackerFormat(
            &hcamera_dcmipp, DCMIPP_PIPE1, pipe.PixelPackerFormat);
        if (status == HAL_OK) {
            status = HAL_DCMIPP_PIPE_SetPitch(&hcamera_dcmipp,
                                              DCMIPP_PIPE1,
                                              pipe.PixelPipePitch);
        }
    }

    return status == HAL_OK ? uai::driver::DriverStatus::kOk
                            : uai::driver::DriverStatus::kHardwareError;
}

} // namespace

namespace uai::driver::arch {

DriverStatus InitializeCamera()
{
    if (!IsOk(InitializeMedia())) {
        return DriverStatus::kHardwareError;
    }

    if (BSP_CAMERA_Init(0, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
            BSP_ERROR_NONE ||
        !IsOk(ConfigureSensorFrameRate()) ||
        !IsOk(InstallImx335ExposureWorkaround())) {
        return DriverStatus::kHardwareError;
    }
    return ConfigureCapturePipe();
}

DriverStatus StartCamera()
{
    completed_camera_frame = 0;
    camera_frame_buffer0 = 0x34200000UL;
    camera_frame_buffer1 = 0x342E0000UL;
    active_camera_frame = camera_frame_buffer0;
    next_camera_frame = camera_frame_buffer1;

    if (BSP_CAMERA_Start(0, reinterpret_cast<uint8_t *>(active_camera_frame),
                         CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    const DriverStatus status = RestartImx335Streaming();
    if (IsOk(status)) {
        last_camera_frame_tick = HAL_GetTick();
        last_processed_vsync_count = g_camera_vsync_event_count;
        camera_recovery_attempted = false;
    }
    return status;
}

DriverStatus StartCamera(std::uintptr_t first_buffer,
                         std::uintptr_t second_buffer)
{
    completed_camera_frame = 0;
    camera_frame_buffer0 = first_buffer;
    camera_frame_buffer1 = second_buffer;
    active_camera_frame = camera_frame_buffer0;
    next_camera_frame = camera_frame_buffer1;

    if (BSP_CAMERA_Start(0, reinterpret_cast<uint8_t *>(active_camera_frame),
                         CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    const DriverStatus status = RestartImx335Streaming();
    if (IsOk(status)) {
        last_camera_frame_tick = HAL_GetTick();
        last_processed_vsync_count = g_camera_vsync_event_count;
        camera_recovery_attempted = false;
    }
    return status;
}

DriverStatus ProcessCamera()
{
    const uint32_t now = HAL_GetTick();
    const uint32_t frame_age = now - last_camera_frame_tick;
    const bool retry_allowed =
        !camera_recovery_attempted ||
        (now - last_camera_recovery_tick) >= kCameraRecoveryRetryMs;
    if (frame_age >= kCameraFrameTimeoutMs && retry_allowed) {
        camera_recovery_attempted = true;
        last_camera_recovery_tick = now;
        ++g_camera_recovery_count;
        if (!IsOk(RecoverCameraCapture())) {
            ++g_camera_recovery_error_count;
            /* Keep the render task alive so a later bounded retry can
             * recover transient sensor/CSI failures. */
            return DriverStatus::kOk;
        }
    }

    const uint32_t vsync_count = g_camera_vsync_event_count;
    if (vsync_count != last_processed_vsync_count) {
        last_processed_vsync_count = vsync_count;
        if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE) {
            ++g_camera_isp_error_count;
        }
    }
    return DriverStatus::kOk;
}

std::uintptr_t TakeCompletedCameraFrame()
{
    const std::uintptr_t frame = completed_camera_frame;
    completed_camera_frame = 0;
    return frame;
}

} // namespace uai::driver::arch

/* Cube FW N6 V1.3.0 truncates the IMX335 line period (7.407 us) to 7 us in
 * IMX335_SetExposure(). At 33.266 ms that requests more than VMAX lines and
 * underflows the unsigned shutter calculation. Convert using the reference
 * driver's fractional line period, then pass an exact multiple of the BSP's
 * integer period so its division produces the intended line count. */
extern "C" ISP_StatusTypeDef Sample2SetImx335Exposure(uint32_t instance,
                                                       int32_t exposure)
{
    (void)instance;
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr || exposure < 0) {
        return ISP_ERR_EINVAL;
    }

    g_sample2_last_exposure_request_us = static_cast<uint32_t>(exposure);
    uint32_t exposure_lines =
        static_cast<uint32_t>(static_cast<float>(exposure) /
                              kImx335LinePeriodUs);
    const uint32_t maximum_safe_lines =
        kImx335VmaxAt30Fps - kImx335MinimumShutterLines;
    if (exposure_lines > maximum_safe_lines) {
        exposure_lines = maximum_safe_lines;
    }
    g_sample2_last_exposure_lines = exposure_lines;

    const int32_t bsp_exposure_us =
        static_cast<int32_t>(exposure_lines * kCubeImx335LinePeriodUs);
    return IMX335_SetExposure(sensor, bsp_exposure_us) == IMX335_OK
               ? ISP_OK
               : ISP_ERR_EINVAL;
}

extern "C" ISP_StatusTypeDef Sample2GetImx335Exposure(uint32_t instance,
                                                       int32_t *exposure)
{
    (void)instance;
    if (Camera_CompObj == nullptr || exposure == nullptr) {
        return ISP_ERR_EINVAL;
    }
    *exposure = static_cast<int32_t>(g_sample2_last_exposure_request_us);
    return ISP_OK;
}

extern "C" ISP_StatusTypeDef Sample2SetImx335Gain(uint32_t instance,
                                                   int32_t gain_mdB)
{
    (void)instance;
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr || gain_mdB < IMX335_GAIN_MIN) {
        return ISP_ERR_EINVAL;
    }

    const int32_t applied_gain_mdB =
        gain_mdB > kImx335MaximumGainMdB ? kImx335MaximumGainMdB : gain_mdB;
    if (IMX335_SetGain(sensor, applied_gain_mdB) != IMX335_OK) {
        return ISP_ERR_EINVAL;
    }
    g_sample2_last_sensor_gain_mdB =
        static_cast<unsigned int>(applied_gain_mdB);
    return ISP_OK;
}

extern "C" ISP_StatusTypeDef Sample2GetImx335Gain(uint32_t instance,
                                                   int32_t *gain_mdB)
{
    (void)instance;
    if (Camera_CompObj == nullptr || gain_mdB == nullptr) {
        return ISP_ERR_EINVAL;
    }
    *gain_mdB = static_cast<int32_t>(g_sample2_last_sensor_gain_mdB);
    return ISP_OK;
}

extern "C" int32_t Sample2GetSensorGainMdB()
{
    if (hcamera_isp.appliHelpers.GetSensorGain == nullptr) {
        return -1;
    }

    int32_t gain_mdB = -1;
    if (hcamera_isp.appliHelpers.GetSensorGain(0U, &gain_mdB) != ISP_OK) {
        return -1;
    }
    return gain_mdB;
}

extern "C" int32_t Sample2ReadSensorRegisters(uint32_t *vmax,
                                               uint32_t *shutter,
                                               uint32_t *gain)
{
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr || vmax == nullptr || shutter == nullptr ||
        gain == nullptr) {
        return -1;
    }

    uint8_t raw_vmax[3] = {};
    uint8_t raw_shutter[3] = {};
    uint8_t raw_gain[2] = {};
    if (imx335_read_reg(&sensor->Ctx, IMX335_REG_VMAX, raw_vmax,
                        sizeof(raw_vmax)) != IMX335_OK ||
        imx335_read_reg(&sensor->Ctx, IMX335_REG_SHUTTER, raw_shutter,
                        sizeof(raw_shutter)) != IMX335_OK ||
        imx335_read_reg(&sensor->Ctx, IMX335_REG_GAIN, raw_gain,
                        sizeof(raw_gain)) != IMX335_OK) {
        return -1;
    }

    *vmax = static_cast<uint32_t>(raw_vmax[0]) |
            (static_cast<uint32_t>(raw_vmax[1]) << 8U) |
            (static_cast<uint32_t>(raw_vmax[2]) << 16U);
    *shutter = static_cast<uint32_t>(raw_shutter[0]) |
               (static_cast<uint32_t>(raw_shutter[1]) << 8U) |
               (static_cast<uint32_t>(raw_shutter[2]) << 16U);
    *gain = static_cast<uint32_t>(raw_gain[0]) |
            (static_cast<uint32_t>(raw_gain[1]) << 8U);
    return 0;
}

/* Match the ref application's PIPE1 callback: install the next capture
 * address at frame end, then publish the buffer that has just completed. */
extern "C" void BSP_CAMERA_FrameEventCallback(uint32_t Instance)
{
    (void)Instance;
    ++g_camera_frame_event_count;
    if (HAL_DCMIPP_PIPE_SetMemoryAddress(
            &hcamera_dcmipp, DCMIPP_PIPE1, DCMIPP_MEMORY_ADDRESS_0,
            static_cast<uint32_t>(next_camera_frame)) != HAL_OK) {
        ++g_camera_dcmipp_error_count;
        return;
    }
    last_camera_frame_tick = HAL_GetTick();
    camera_recovery_attempted = false;
    completed_camera_frame = active_camera_frame;
    active_camera_frame = next_camera_frame;
    next_camera_frame = (next_camera_frame == camera_frame_buffer0)
                            ? camera_frame_buffer1
                            : camera_frame_buffer0;
}

extern "C" void BSP_CAMERA_VsyncEventCallback(uint32_t Instance)
{
    (void)Instance;
    ++g_camera_vsync_event_count;
}

/* Keep the camera IRQ handlers in the same object as InitializeCamera().
 * Startup code supplies weak defaults, so a standalone IRQ object inside a
 * static library would otherwise not be extracted by the linker. */
extern "C" void DCMIPP_IRQHandler(void)
{
    g_camera_dcmipp_last_status = hcamera_dcmipp.Instance->CMSR2;
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}

extern "C" void CSI_IRQHandler(void)
{
    const uint32_t status0 = CSI->SR0;
    const uint32_t status1 = CSI->SR1;
    const uint32_t interrupt_enable0 = CSI->IER0;
    const uint32_t interrupt_enable1 = CSI->IER1;
    const uint32_t previous_error_code = hcamera_dcmipp.ErrorCode;
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);

    /* HAL disables an error interrupt after servicing it, but the raw status
     * bit may remain set while later frame interrupts continue.  Count only
     * a newly latched HAL error (or a live lane-error flag), not that stale
     * raw bit on every subsequent IRQ. */
    const uint32_t new_error_code =
        hcamera_dcmipp.ErrorCode & ~previous_error_code;
    const bool newly_enabled_error =
        (status0 & interrupt_enable0 & CSI_SR0_SYNCERRF) != 0U ||
        (status1 & interrupt_enable1 & 0x00001F1FU) != 0U;
    if (new_error_code != 0U || newly_enabled_error) {
        g_camera_csi_last_status = status0;
        g_camera_csi_last_status1 = status1;
        g_camera_csi_last_error_code = new_error_code;
        ++g_camera_csi_error_count;
    }
}

extern "C" void BSP_CAMERA_PipeErrorCallback(uint32_t Instance)
{
    (void)Instance;
    ++g_camera_dcmipp_error_count;
}

extern "C" void BSP_CAMERA_ErrorCallback(uint32_t Instance)
{
    (void)Instance;
    ++g_camera_camera_error_count;
}

/* The BSP hook is weak.  Use the same dedicated media clock as ref. */
extern "C" HAL_StatusTypeDef MX_DCMIPP_ClockConfig(
    DCMIPP_HandleTypeDef *hdcmipp)
{
    UNUSED(hdcmipp);

    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP;
    clock.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
    clock.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
    clock.ICSelection[RCC_IC17].ClockDivider = 3U;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        return HAL_ERROR;
    }

    /* The CSI receiver has its own kernel clock.  The ref project programs
     * it explicitly; configuring DCMIPP alone lets camera start succeed but
     * leaves the CSI side unable to produce completed frames. */
    clock.PeriphClockSelection = RCC_PERIPHCLK_CSI;
    clock.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC18].ClockDivider = 40U;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}
