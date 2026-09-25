#include "driver/camera_driver.hpp"

#include <cstdint>

extern "C" {
#include "imx335.h"
#include "isp_api.h"
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"
#include <tm/tmonitor.h>

extern void *Camera_CompObj;
extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern ISP_HandleTypeDef hcamera_isp;
}

#include "driver/frame_buffer.hpp"

namespace uai::camera_pipe2::driver {

namespace {

constexpr std::uint32_t kSensorWidth = 2592U;
constexpr std::uint32_t kSensorHeight = 1944U;
constexpr std::uint32_t kOutputWidth = static_cast<std::uint32_t>(kFrameWidth);
constexpr std::uint32_t kOutputHeight = static_cast<std::uint32_t>(kFrameHeight);
#if PIPE2_LAB_CROP_NATIVE
constexpr std::uint32_t kCropWidth = kOutputWidth;
constexpr std::uint32_t kCropHeight = kOutputHeight;
constexpr std::uint32_t kCropHorizontalStart =
    (kSensorWidth - kCropWidth) / 2U;
constexpr std::uint32_t kCropVerticalStart =
    (kSensorHeight - kCropHeight) / 2U;
#elif PIPE2_LAB_CROP_INTEGER4
constexpr std::uint32_t kCropWidth = 1600U;
constexpr std::uint32_t kCropHeight = 1920U;
constexpr std::uint32_t kCropHorizontalStart =
    (kSensorWidth - kCropWidth) / 2U;
constexpr std::uint32_t kCropVerticalStart =
    (kSensorHeight - kCropHeight) / 2U;
#elif PIPE2_LAB_PIPE_DUAL
constexpr std::uint32_t kCropWidth = 1620U;
constexpr std::uint32_t kCropHeight = kSensorHeight;
constexpr std::uint32_t kCropHorizontalStart = 0U;
constexpr std::uint32_t kCropVerticalStart = 0U;
#else
/* Single-pipe comparison uses the full sensor width and a centered 16:9 crop. */
constexpr std::uint32_t kCropWidth = kSensorWidth;
constexpr std::uint32_t kCropHeight = 1555U;
constexpr std::uint32_t kCropHorizontalStart = 0U;
constexpr std::uint32_t kCropVerticalStart = (kSensorHeight - kCropHeight) / 2U;
#endif

DriverStatus ConfigureSensorProfile()
{
#if PIPE2_LAB_IMX335_MIPI891
    /* This is the old sample-ai 891 Mbps sensor profile.  Put the sensor in
     * standby before changing its MIPI timing, then resume it once all values
     * are committed. */
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr || sensor->IO.WriteReg == nullptr) {
        return DriverStatus::kHardwareFailure;
    }
    const std::uint8_t standby = 0x01U;
    if (sensor->IO.WriteReg(sensor->IO.Address, 0x3000U,
                            const_cast<std::uint8_t *>(&standby), 1U) != 0) {
        return DriverStatus::kHardwareFailure;
    }
    const std::uint8_t clock_select[] = {0x29U, 0x01U};
    const std::uint8_t lane_clock = 0x06U;
    const std::uint8_t system_mode = 0x02U;
    if (sensor->IO.WriteReg(sensor->IO.Address, 0x314CU,
                            const_cast<std::uint8_t *>(clock_select), 2U) != 0 ||
        sensor->IO.WriteReg(sensor->IO.Address, 0x315AU,
                            const_cast<std::uint8_t *>(&lane_clock), 1U) != 0 ||
        sensor->IO.WriteReg(sensor->IO.Address, 0x319EU,
                            const_cast<std::uint8_t *>(&system_mode), 1U) != 0) {
        return DriverStatus::kHardwareFailure;
    }
    constexpr std::uint16_t timing_addresses[] = {
        0x3A18U, 0x3A1AU, 0x3A1CU, 0x3A1EU, 0x3A20U,
        0x3A22U, 0x3A24U, 0x3A26U, 0x3A28U};
    constexpr std::uint8_t timing_values[] = {
        0x7FU, 0x00U, 0x37U, 0x00U, 0x37U, 0x00U, 0xF7U, 0x00U,
        0x3FU, 0x00U, 0x6FU, 0x00U, 0x3FU, 0x00U, 0x5FU, 0x00U,
        0x2FU, 0x00U};
    for (std::size_t index = 0U; index < 9U; ++index) {
        if (sensor->IO.WriteReg(sensor->IO.Address, timing_addresses[index],
                                const_cast<std::uint8_t *>(&timing_values[index * 2U]),
                                2U) != 0) {
            return DriverStatus::kHardwareFailure;
        }
    }
    /* Program the receiver while the sensor remains in standby so its PHY
     * is ready before the first frame is transmitted. */
    DCMIPP_CSI_ConfTypeDef csi{};
    csi.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
    csi.NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES;
    csi.PHYBitrate = DCMIPP_CSI_PHY_BT_900;
    if (HAL_DCMIPP_CSI_SetConfig(&hcamera_dcmipp, &csi) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }

    const std::uint8_t streaming = 0x00U;
    if (sensor->IO.WriteReg(sensor->IO.Address, 0x3000U,
                            const_cast<std::uint8_t *>(
                                &streaming), 1U) != 0) {
        return DriverStatus::kHardwareFailure;
    }
#endif
    return DriverStatus::kOk;
}

DriverStatus ConfigurePipe(std::uint32_t pipe)
{
    DCMIPP_CropConfTypeDef crop{};
    crop.HStart = kCropHorizontalStart;
    crop.VStart = kCropVerticalStart;
    crop.HSize = kCropWidth;
    crop.VSize = kCropHeight;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;

    if (HAL_DCMIPP_PIPE_DisableCrop(&hcamera_dcmipp, pipe) != HAL_OK ||
        HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, pipe, &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, pipe) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }

#if PIPE2_LAB_CROP_NATIVE
    if (HAL_DCMIPP_PIPE_DisableDownsize(&hcamera_dcmipp, pipe) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }
#else
    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = (8192U * crop.HSize) / kOutputWidth;
    downsize.VRatio = (8192U * crop.VSize) / kOutputHeight;
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = kOutputWidth;
    downsize.VSize = kOutputHeight;

    if (HAL_DCMIPP_PIPE_DisableDownsize(&hcamera_dcmipp, pipe) != HAL_OK ||
        HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, pipe, &downsize) !=
            HAL_OK ||
        HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, pipe) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }
#endif

    DCMIPP_PipeConfTypeDef pipe_config{};
    pipe_config.FrameRate = DCMIPP_FRAME_RATE_ALL;
    pipe_config.PixelPipePitch = kOutputWidth * 2U;
    pipe_config.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    if (HAL_DCMIPP_PIPE_SetConfig(&hcamera_dcmipp, pipe, &pipe_config) !=
        HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }

    if (HAL_DCMIPP_PIPE_DisableRedBlueSwap(&hcamera_dcmipp, pipe) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }

    return DriverStatus::kOk;
}

} // namespace

DriverStatus CameraDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    tm_putstring(reinterpret_cast<UB *>(const_cast<char *>(
        "camera_pipe2: BSP camera init begin\n")));
    const int32_t camera_init_status =
        BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera_pipe2: BSP camera init status=%d\n"),
              camera_init_status);
    if (camera_init_status != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

#if PIPE2_LAB_IMX335_TEST_PATTERN_MODE >= 0
    /* Sensor-generated flat field separates capture/display artifacts from
     * scene-dependent sensor output. Mode 2 is the uniform 555h pattern. */
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (sensor == nullptr) {
        return DriverStatus::kHardwareFailure;
    }
    tm_printf(reinterpret_cast<const UB *>(
                  "camera_pipe2: test pattern begin obj=%08x io=%08x ctx=%08x\n"),
              static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sensor)),
              static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sensor->IO.WriteReg)),
              static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sensor->Ctx.WriteReg)));
    const int32_t pattern_status = IMX335_SetTestPattern(
        sensor, PIPE2_LAB_IMX335_TEST_PATTERN_MODE);
    tm_printf(reinterpret_cast<const UB *>(
                  "camera_pipe2: test pattern status=%d\n"), pattern_status);
    if (pattern_status != IMX335_OK) {
        return DriverStatus::kHardwareFailure;
    }
#endif

    if (!IsOk(ConfigureSensorProfile())) {
        return DriverStatus::kHardwareFailure;
    }

#if PIPE2_LAB_PIPE_DUAL
    /* Pipe2 is the ancillary output and shares the ISP input with Pipe1. */
    if (HAL_DCMIPP_PIPE_CSI_EnableShare(&hcamera_dcmipp, DCMIPP_PIPE2) !=
        HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }
#endif

    if (!IsOk(ConfigurePipe(DCMIPP_PIPE1))
#if PIPE2_LAB_PIPE_DUAL
        || !IsOk(ConfigurePipe(DCMIPP_PIPE2))
#endif
    ) {
        return DriverStatus::kHardwareFailure;
    }

    initialized_ = true;
    return DriverStatus::kOk;
}

DriverStatus CameraDriver::Start()
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (started_) {
        return DriverStatus::kAlreadyStarted;
    }

    /* The IMX335 needs time to settle after the BSP reset sequence. */
    HAL_Delay(100U);

    if (HAL_DCMIPP_CSI_PIPE_Start(
            &hcamera_dcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0,
            static_cast<std::uint32_t>(MainPipeFrameBufferAddress()),
            DCMIPP_MODE_CONTINUOUS) != HAL_OK
#if PIPE2_LAB_PIPE_DUAL
        ||
        HAL_DCMIPP_CSI_PIPE_Start(
            &hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0,
            static_cast<std::uint32_t>(AncillaryPipeFrameBufferAddress()),
            DCMIPP_MODE_CONTINUOUS) != HAL_OK
#endif
    ) {
        return DriverStatus::kHardwareFailure;
    }

    if (ISP_Start(&hcamera_isp) != ISP_OK) {
        return DriverStatus::kHardwareFailure;
    }

    started_ = true;
    return DriverStatus::kOk;
}

DriverStatus CameraDriver::Process()
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (!started_) {
        return DriverStatus::kNotStarted;
    }

    return BSP_CAMERA_BackgroundProcess() == BSP_ERROR_NONE
               ? DriverStatus::kOk
               : DriverStatus::kHardwareFailure;
}

} // namespace uai::camera_pipe2::driver
