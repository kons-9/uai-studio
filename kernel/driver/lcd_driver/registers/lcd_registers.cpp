#include "driver/lcd_driver/registers/lcd_registers.hpp"

#include "middleware/memory/static_memory_layout.hpp"
#include "middleware/memory/generated/static_memory_layout/key.hpp"

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
}

namespace uai::ai::lcd::registers {
namespace {

using StaticMemoryKey = static_memory_layout::Key;

constexpr std::uint32_t kDisplayInstance = 0U;
constexpr std::uint32_t kDisplayLayer = 0U;
constexpr std::uint32_t kDisplayWidth = 800U;
constexpr std::uint32_t kDisplayHeight = 480U;

} // namespace

uai::driver::DriverStatus LcdRegisterLayer::Initialize()
{
    if (initialized_) {
        return uai::driver::DriverStatus::kAlreadyInitialized;
    }
    if (BSP_LCD_Init(kDisplayInstance, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    BSP_LCD_LayerConfig_t layer{};
    layer.Address = static_cast<std::uint32_t>(
        static_memory_layout::Region::GetRegionFromKey(StaticMemoryKey::kDisplay0).address()
    );
    layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    layer.X0 = 0U;
    layer.X1 = kDisplayWidth;
    layer.Y0 = 0U;
    layer.Y1 = kDisplayHeight;
    if (BSP_LCD_ConfigLayer(kDisplayInstance, kDisplayLayer, &layer) != BSP_ERROR_NONE) {
        return uai::driver::DriverStatus::kHardwareError;
    }
    if (BSP_LCD_SetLayerVisible(kDisplayInstance, kDisplayLayer, DISABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(kDisplayInstance, 1U, DISABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetActiveLayer(kDisplayInstance, kDisplayLayer) != BSP_ERROR_NONE
        || BSP_LCD_DisplayOn(kDisplayInstance) != BSP_ERROR_NONE) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    initialized_ = true;
    return uai::driver::DriverStatus::kOk;
}

uai::driver::DriverStatus LcdRegisterLayer::Synchronize()
{
    if (!initialized_) {
        return uai::driver::DriverStatus::kNotInitialized;
    }
    if (!reload_pending_) {
        return uai::driver::DriverStatus::kOk;
    }
    /* experiment-ai runs in the Secure CM55 build (-mcmse). */
    if (__HAL_LTDC_GET_FLAG(&hlcd_ltdc, LTDC_FLAG_RR) == 0U) {
        return uai::driver::DriverStatus::kBusy;
    }

    __HAL_LTDC_CLEAR_FLAG(&hlcd_ltdc, LTDC_FLAG_RR);
    reload_pending_ = false;
    return uai::driver::DriverStatus::kOk;
}

uai::driver::DriverStatus LcdRegisterLayer::Present(std::uintptr_t buffer)
{
    if (!initialized_) {
        return uai::driver::DriverStatus::kNotInitialized;
    }
    if (buffer == 0U
        || BSP_LCD_SetLayerAddress(kDisplayInstance, kDisplayLayer, static_cast<std::uint32_t>(buffer))
            != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(kDisplayInstance, kDisplayLayer, ENABLE) != BSP_ERROR_NONE
        || BSP_LCD_Reload(kDisplayInstance, BSP_LCD_RELOAD_VERTICAL_BLANKING) != BSP_ERROR_NONE) {
        return uai::driver::DriverStatus::kHardwareError;
    }
    reload_pending_ = true;
    return uai::driver::DriverStatus::kOk;
}

} // namespace uai::ai::lcd::registers
