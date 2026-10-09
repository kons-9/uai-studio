#include "bsp_device.hpp"
#include "driver/frame_buffer.hpp"

extern "C" {
#include "imx335.h"
#include "isp_api.h"
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"
extern ISP_HandleTypeDef hcamera_isp;
extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern void *Camera_CompObj;
}

namespace experiment::camera {

namespace {
decltype(ISP_AppliHelpersTypeDef::SetSensorExposure) original_exposure = nullptr;
ISP_StatusTypeDef LimitedExposure(
    std::uint32_t instance,
    std::int32_t exposure
)
{
    if (!original_exposure || exposure < 0) {
        return ISP_ERR_EINVAL;
    }
    const auto maximum = hcamera_isp.sensorInfo.exposure_max;
    return original_exposure(
        instance, static_cast<std::uint32_t>(exposure) > maximum ? static_cast<std::int32_t>(maximum) : exposure
    );
}
}

console::Status BspDevice::Open()
{
    if (opened_) {
        return console::Status::kInvalidState;
    }
    camera_ = {};
    camera_started_ = false;
    opened_ = true;
    if (!uai::camera_pipe2::driver::IsOk(camera_.Initialize())) {
        return console::Status::kHardware;
    }
    original_exposure = hcamera_isp.appliHelpers.SetSensorExposure;
    hcamera_isp.appliHelpers.SetSensorExposure = LimitedExposure;
    return console::Status::kOk;
}

console::Status BspDevice::Close()
{
    if (!opened_) {
        return console::Status::kOk;
    }
    HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0);
    const auto status = BSP_CAMERA_DeInit(0);
    if (status == BSP_ERROR_NONE) {
        opened_ = false;
        camera_started_ = false;
    }
    return status == BSP_ERROR_NONE ? console::Status::kOk : console::Status::kHardware;
}

console::Status BspDevice::Poll()
{
    return uai::camera_pipe2::driver::IsOk(camera_.Process()) ? console::Status::kOk : console::Status::kHardware;
}

console::Status BspDevice::Configure(const Geometry &geometry)
{
    const bool was_started = camera_started_;
    auto *sensor = static_cast<IMX335_Object_t *>(Camera_CompObj);
    if (!opened_ || !sensor || !sensor->IO.ReadReg || !sensor->IO.WriteReg) {
        return console::Status::kInvalidState;
    }
    if (geometry.fps < 10 || geometry.fps > 30 || geometry.fps % 5 != 0
        || !Inside(geometry.crop, hcamera_isp.sensorInfo.width, hcamera_isp.sensorInfo.height)
        || geometry.crop.width < 400 || geometry.crop.height < 480) {
        return console::Status::kInvalidArgument;
    }
    std::uint8_t automatic = 0;
    if (ISP_GetAECState(&hcamera_isp, &automatic) != ISP_OK || ISP_SetAECState(&hcamera_isp, 0) != ISP_OK) {
        return console::Status::kHardware;
    }
    std::uint8_t standby = 1;
    if (sensor->IO.WriteReg(sensor->IO.Address, 0x3000, &standby, 1) != 0) {
        return console::Status::kHardware;
    }
    bool success = IMX335_SetFramerate(sensor, static_cast<std::int32_t>(geometry.fps)) == IMX335_OK;
    const auto flip = (geometry.horizontal ? IMX335_MIRROR : 0U) | (geometry.vertical ? IMX335_FLIP : 0U);
    success = IMX335_MirrorFlipConfig(sensor, flip) == IMX335_OK && success;
    std::uint8_t frame_length[3]{};
    success = sensor->IO.ReadReg(sensor->IO.Address, 0x3030, frame_length, 3) == 0 && success;
    const auto lines =
        std::uint32_t(frame_length[0]) | (std::uint32_t(frame_length[1]) << 8) | (std::uint32_t(frame_length[2]) << 16);
    if (lines <= 9 || lines > 0xfffff) {
        success = false;
    }
    if (success) {
        hcamera_isp.sensorInfo.exposure_max = (lines - 9) * 7;
        std::int32_t exposure = 0;
        success = hcamera_isp.appliHelpers.GetSensorExposure(0, &exposure) == ISP_OK;
        if (success && exposure > 0) {
            success = LimitedExposure(0, exposure) == ISP_OK;
        }
    }
    const auto &rectangle = geometry.crop;
    DCMIPP_CropConfTypeDef crop{};
    crop.HStart = rectangle.x;
    crop.VStart = rectangle.y;
    crop.HSize = rectangle.width;
    crop.VSize = rectangle.height;
    crop.PipeArea = DCMIPP_POSITIVE_AREA;
    DCMIPP_DownsizeTypeDef downsize{};
    downsize.HRatio = 8192U * rectangle.width / 400U;
    downsize.VRatio = 8192U * rectangle.height / 480U;
    downsize.HDivFactor = (1024U * 8192U - 1U) / downsize.HRatio;
    downsize.VDivFactor = (1024U * 8192U - 1U) / downsize.VRatio;
    downsize.HSize = 400;
    downsize.VSize = 480;
    if (success) {
        success =
            (!was_started || HAL_DCMIPP_CSI_PIPE_Stop(&hcamera_dcmipp, DCMIPP_PIPE2, DCMIPP_VIRTUAL_CHANNEL0) == HAL_OK)
            && HAL_DCMIPP_PIPE_DisableCrop(&hcamera_dcmipp, DCMIPP_PIPE2) == HAL_OK
            && HAL_DCMIPP_PIPE_SetCropConfig(&hcamera_dcmipp, DCMIPP_PIPE2, &crop) == HAL_OK
            && HAL_DCMIPP_PIPE_EnableCrop(&hcamera_dcmipp, DCMIPP_PIPE2) == HAL_OK
            && HAL_DCMIPP_PIPE_DisableDownsize(&hcamera_dcmipp, DCMIPP_PIPE2) == HAL_OK
            && HAL_DCMIPP_PIPE_SetDownsizeConfig(&hcamera_dcmipp, DCMIPP_PIPE2, &downsize) == HAL_OK
            && HAL_DCMIPP_PIPE_EnableDownsize(&hcamera_dcmipp, DCMIPP_PIPE2) == HAL_OK
            && (!was_started
                || HAL_DCMIPP_CSI_PIPE_Start(
                       &hcamera_dcmipp,
                       DCMIPP_PIPE2,
                       DCMIPP_VIRTUAL_CHANNEL0,
                       static_cast<std::uint32_t>(uai::camera_pipe2::driver::AncillaryPipeFrameBufferAddress()),
                       DCMIPP_MODE_CONTINUOUS
                   ) == HAL_OK);
    }
    standby = 0;
    success = sensor->IO.WriteReg(sensor->IO.Address, 0x3000, &standby, 1) == 0 && success;
    success = ISP_SetAECState(&hcamera_isp, automatic) == ISP_OK && success;
    if (success && !was_started) {
        success = uai::camera_pipe2::driver::IsOk(camera_.Start());
        if (success) {
            camera_started_ = true;
        }
    }
    return success ? console::Status::kOk : console::Status::kHardware;
}

}
