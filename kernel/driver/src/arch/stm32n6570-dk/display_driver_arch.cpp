#include "driver_arch.hpp"

extern "C" {
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6xx_hal.h"
}

namespace uai::driver::arch {

DriverStatus InitializeDisplay()
{
    if (!IsOk(InitializeMedia())) {
        return DriverStatus::kHardwareError;
    }

    if (BSP_LCD_Init(0, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    BSP_LCD_DisplayOn(0);

    /* Do not expose the capture buffer until the camera has completed a
     * frame. The LTDC background color is black while the layer is hidden. */
    if (BSP_LCD_SetLayerVisible(0, 0, DISABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    return DriverStatus::kOk;
}

DriverStatus ProcessDisplay()
{
    const std::uintptr_t frame = TakeCompletedCameraFrame();
    if (frame == 0) {
        return DriverStatus::kOk;
    }

    /* Stage the address and visibility change, then apply both at the next
     * vertical blanking period so LTDC never scans a partially switched frame. */
    if (BSP_LCD_Reload(0, BSP_LCD_RELOAD_NONE) != BSP_ERROR_NONE ||
        BSP_LCD_SetLayerAddress(0, 0, static_cast<uint32_t>(frame)) !=
            BSP_ERROR_NONE ||
        BSP_LCD_SetLayerVisible(0, 0, ENABLE) != BSP_ERROR_NONE ||
        BSP_LCD_Reload(0, BSP_LCD_RELOAD_VERTICAL_BLANKING) !=
            BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    return DriverStatus::kOk;
}

} // namespace uai::driver::arch

/* The DK BSP expects this CubeMX hook, while the common clock setup is in
 * the pre-kernel project. The panel uses a 25 MHz LTDC pixel clock. */
extern "C" HAL_StatusTypeDef MX_LTDC_ClockConfig(LTDC_HandleTypeDef *hltdc)
{
    UNUSED(hltdc);

    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clock.LtdcClockSelection = RCC_LTDCCLKSOURCE_IC16;
    clock.ICSelection[RCC_IC16].ClockSelection = RCC_ICCLKSOURCE_PLL1;
    clock.ICSelection[RCC_IC16].ClockDivider = 48;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}
