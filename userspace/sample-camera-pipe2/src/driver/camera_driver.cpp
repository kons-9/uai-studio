#include "driver/camera_driver.hpp"

#include <cstdint>

extern "C" {
#include "isp_api.h"
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"

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
constexpr std::uint32_t kCropWidth = 1620U;
constexpr std::uint32_t kCropHeight = kSensorHeight;

DriverStatus ConfigurePipe(std::uint32_t pipe, std::uint32_t horizontal_start)
{
    DCMIPP_CropConfTypeDef crop{};
    crop.HStart = horizontal_start;
    crop.VStart = 0U;
    crop.HSize = kCropWidth;
    crop.VSize = kCropHeight;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;

    if (HAL_DCMIPP_PIPE_DisableCrop(&hcamera_dcmipp, pipe) != HAL_OK ||
        HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, pipe, &crop) != HAL_OK ||
        HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, pipe) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }

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

    if (BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
        BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    /* Pipe2 is the ancillary output and shares the ISP input with Pipe1. */
    if (HAL_DCMIPP_PIPE_CSI_EnableShare(&hcamera_dcmipp, DCMIPP_PIPE2) !=
        HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }

    /* Temporarily show the same sensor crop on both pipes. */
    if (!IsOk(ConfigurePipe(DCMIPP_PIPE1, 0U)) ||
        !IsOk(ConfigurePipe(DCMIPP_PIPE2, 0U))) {
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
            DCMIPP_MODE_CONTINUOUS) != HAL_OK ||
        HAL_DCMIPP_CSI_PIPE_Start(
            &hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0,
            static_cast<std::uint32_t>(AncillaryPipeFrameBufferAddress()),
            DCMIPP_MODE_CONTINUOUS) != HAL_OK) {
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
