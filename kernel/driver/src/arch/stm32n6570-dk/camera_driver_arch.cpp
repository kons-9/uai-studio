#include "driver_arch.hpp"

#include <cstdint>

extern "C" {
#include "stm32n6570_discovery_camera.h"
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
}

namespace {

constexpr uintptr_t kCameraFrameBuffer = 0x34200000UL;

} // namespace

namespace uai::driver::arch {

DriverStatus InitializeCamera()
{
    if (!IsOk(InitializeMedia())) {
        return DriverStatus::kHardwareError;
    }

    if (BSP_CAMERA_Init(0, CAMERA_R2592x1944, CAMERA_PF_RAW_RGGB10) !=
        BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    return DriverStatus::kOk;
}

DriverStatus StartCamera()
{
    if (BSP_CAMERA_Start(0, reinterpret_cast<uint8_t *>(kCameraFrameBuffer),
                         CAMERA_MODE_CONTINUOUS) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    return DriverStatus::kOk;
}

DriverStatus ProcessCamera()
{
    if (BSP_CAMERA_BackgroundProcess() != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    return DriverStatus::kOk;
}

} // namespace uai::driver::arch

/* Keep the camera IRQ handlers in the same object as InitializeCamera().
 * Startup code supplies weak defaults, so a standalone IRQ object inside a
 * static library would otherwise not be extracted by the linker. */
extern "C" void DCMIPP_IRQHandler(void)
{
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}

extern "C" void CSI_IRQHandler(void)
{
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);
}
