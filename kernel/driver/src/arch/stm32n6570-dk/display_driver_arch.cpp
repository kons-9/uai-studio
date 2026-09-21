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
