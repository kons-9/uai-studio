#include "driver/lcd_driver/lcd_hardware.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
}

namespace uai::driver {
namespace {

constexpr std::uint32_t kDisplayInstance = 0U;
constexpr std::uint32_t kDisplayLayer = 0U;
constexpr std::uint32_t kDisplayWidth = 800U;
constexpr std::uint32_t kDisplayHeight = 480U;
constexpr std::uint32_t kInitialDisplayBuffer = 0x91200000U;

} // namespace

DriverStatus LcdHardware::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }
    if (BSP_LCD_Init(kDisplayInstance, LCD_ORIENTATION_LANDSCAPE) !=
            BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    BSP_LCD_LayerConfig_t layer{};
    layer.Address = kInitialDisplayBuffer;
    layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    layer.X0 = 0U;
    layer.X1 = kDisplayWidth;
    layer.Y0 = 0U;
    layer.Y1 = kDisplayHeight;
    if (BSP_LCD_ConfigLayer(kDisplayInstance, kDisplayLayer, &layer) !=
        BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    if (BSP_LCD_SetLayerVisible(kDisplayInstance, kDisplayLayer, DISABLE) !=
            BSP_ERROR_NONE ||
        BSP_LCD_SetActiveLayer(kDisplayInstance, kDisplayLayer) !=
            BSP_ERROR_NONE ||
        BSP_LCD_DisplayOn(kDisplayInstance) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    initialized_ = true;
    return DriverStatus::kOk;
}

DriverStatus LcdHardware::Synchronize()
{
    return initialized_ ? DriverStatus::kOk : DriverStatus::kNotInitialized;
}

DriverStatus LcdHardware::Process()
{
    return initialized_ ? DriverStatus::kOk : DriverStatus::kNotInitialized;
}

DriverStatus LcdHardware::Process(std::uintptr_t buffer)
{
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }
    if (buffer == 0U ||
        BSP_LCD_SetLayerAddress(kDisplayInstance, kDisplayLayer,
                                static_cast<std::uint32_t>(buffer)) !=
            BSP_ERROR_NONE ||
        BSP_LCD_SetLayerVisible(kDisplayInstance, kDisplayLayer, ENABLE) !=
            BSP_ERROR_NONE ||
        BSP_LCD_Reload(kDisplayInstance,
                       BSP_LCD_RELOAD_VERTICAL_BLANKING) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    return DriverStatus::kOk;
}

} // namespace uai::driver
