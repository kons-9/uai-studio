#include "driver/display_driver.hpp"

#include <cstdint>

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
}

#include "driver/frame_buffer.hpp"

namespace uai::sample1::driver {

DriverStatus DisplayDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    if (BSP_LCD_Init(0U, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    BSP_LCD_LayerConfig_t camera_layer;
    camera_layer.Address = static_cast<std::uint32_t>(FrameBufferAddress());
    camera_layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    camera_layer.X0 = 0U;
    camera_layer.X1 = 800U;
    camera_layer.Y0 = 0U;
    camera_layer.Y1 = 480U;

    if (BSP_LCD_ConfigLayer(0U, 0U, &camera_layer) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_SetLayerVisible(0U, 0U, ENABLE) != BSP_ERROR_NONE ||
        BSP_LCD_SetLayerVisible(0U, 1U, DISABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_SetActiveLayer(0U, 0U) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_DisplayOn(0U) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    initialized_ = true;
    return DriverStatus::kOk;
}

DriverStatus DisplayDriver::Process()
{
    return initialized_ ? DriverStatus::kOk : DriverStatus::kNotInitialized;
}

} // namespace uai::sample1::driver
