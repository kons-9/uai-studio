#include "driver_arch.hpp"

#include <cstdint>

extern "C" {
#include "stm32n6570_discovery_camera.h"
#include "stm32n6xx_hal.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
}

namespace {

constexpr std::uintptr_t kCameraFrameBuffer0 = 0x34200000UL;
constexpr std::uintptr_t kCameraFrameBuffer1 = 0x342E0000UL;

volatile std::uintptr_t completed_camera_frame = 0;
std::uintptr_t next_camera_frame = kCameraFrameBuffer0;

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
    completed_camera_frame = 0;
    next_camera_frame = kCameraFrameBuffer0;

    if (BSP_CAMERA_DoubleBufferStart(
            0, reinterpret_cast<uint8_t *>(kCameraFrameBuffer0),
            reinterpret_cast<uint8_t *>(kCameraFrameBuffer1),
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

std::uintptr_t TakeCompletedCameraFrame()
{
    const std::uintptr_t frame = completed_camera_frame;
    completed_camera_frame = 0;
    return frame;
}

} // namespace uai::driver::arch

/* Publish only complete frames. The DCMIPP double-buffer engine switches to
 * the other destination at the frame boundary, so the buffer used for the
 * completed frame is no longer being written when this callback runs. */
extern "C" void BSP_CAMERA_FrameEventCallback(uint32_t Instance)
{
    (void)Instance;
    completed_camera_frame = next_camera_frame;
    next_camera_frame = (next_camera_frame == kCameraFrameBuffer0)
                            ? kCameraFrameBuffer1
                            : kCameraFrameBuffer0;
}

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
