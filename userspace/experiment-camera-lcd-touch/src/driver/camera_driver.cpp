#include "driver/camera_driver.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"
}

#include "driver/frame_buffer.hpp"

namespace uai::camera_lcd_touch::driver {

DriverStatus CameraDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    if (BSP_CAMERA_Init(0U, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
        BSP_ERROR_NONE) {
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

    if (BSP_CAMERA_Start(0U, FrameBuffer(), CAMERA_MODE_CONTINUOUS) !=
        BSP_ERROR_NONE) {
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

} // namespace uai::camera_lcd_touch::driver
