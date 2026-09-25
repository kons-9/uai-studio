#include "driver_arch.hpp"

extern "C" {
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6xx_hal.h"
#include <tm/tmonitor.h>
}

namespace uai::driver::arch {
namespace {

std::uintptr_t pending_frame_address = 0U;
uint32_t verified_frame_count = 0U;

void ClearReloadFlag()
{
    /* The BSP's LTDC handle is private to its C file. The clear-flag macro
     * only uses Instance, so this local handle safely targets the same block. */
    LTDC_HandleTypeDef ltdc = {};
    ltdc.Instance = LTDC;
    __HAL_LTDC_CLEAR_FLAG(&ltdc, LTDC_FLAG_RR);
}

bool VerifyDisplayedFrame()
{
    if (pending_frame_address == 0U) {
        return true;
    }

    const uint32_t control = LTDC_Layer1->CR;
    const uint32_t address = LTDC_Layer1->CFBAR;
    if ((LTDC->GCR & LTDC_GCR_LTDCEN) == 0U ||
        (control & LTDC_LxCR_LEN) == 0U ||
        address != static_cast<uint32_t>(pending_frame_address)) {
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: layer verify failed gcr=%x cr=%x fb=%x expected=%x\n"),
                  static_cast<unsigned int>(LTDC->GCR),
                  static_cast<unsigned int>(control),
                  static_cast<unsigned int>(address),
                  static_cast<unsigned int>(pending_frame_address));
        return false;
    }

    if (verified_frame_count++ == 0U) {
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: layer verified fb=%x cr=%x\n"),
                  static_cast<unsigned int>(address),
                  static_cast<unsigned int>(control));
    }
    pending_frame_address = 0U;
    return true;
}

DriverStatus WaitForPendingDisplayFrame()
{
    if (pending_frame_address == 0U) {
        return DriverStatus::kOk;
    }

    constexpr uint32_t kReloadTimeoutMs = 40U;
    const uint32_t wait_start = HAL_GetTick();
    for (;;) {
        const uint32_t control = LTDC_Layer1->CR;
        const uint32_t address = LTDC_Layer1->CFBAR;
        if ((LTDC->GCR & LTDC_GCR_LTDCEN) == 0U ||
            (control & LTDC_LxCR_LEN) == 0U) {
            return DriverStatus::kHardwareError;
        }
        /* CFBAR is the shadow configuration register and can match before the
         * VBlank reload. RRIF is set by LTDC only after the reload completes. */
        if ((LTDC->ISR & LTDC_ISR_RRIF) != 0U) {
            ClearReloadFlag();
            return VerifyDisplayedFrame() ? DriverStatus::kOk
                                          : DriverStatus::kHardwareError;
        }
        if ((HAL_GetTick() - wait_start) >= kReloadTimeoutMs) {
            tm_printf(reinterpret_cast<const UB *>(
                          "lcd: reload wait timeout fb=%x expected=%x isr=%x\n"),
                      static_cast<unsigned int>(address),
                      static_cast<unsigned int>(pending_frame_address),
                      static_cast<unsigned int>(LTDC->ISR));
            return DriverStatus::kBusy;
        }
    }
}

} // namespace

DriverStatus InitializeDisplay()
{
    if (!IsOk(InitializeMedia())) {
        return DriverStatus::kHardwareError;
    }

    if (BSP_LCD_Init(0, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    if (BSP_LCD_DisplayOn(0) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    uint32_t width = 0U;
    uint32_t height = 0U;
    uint32_t pixel_format = 0U;
    if (BSP_LCD_GetXSize(0U, &width) != BSP_ERROR_NONE ||
        BSP_LCD_GetYSize(0U, &height) != BSP_ERROR_NONE ||
        BSP_LCD_GetPixelFormat(0U, &pixel_format) != BSP_ERROR_NONE ||
        width != LCD_DEFAULT_WIDTH || height != LCD_DEFAULT_HEIGHT ||
        pixel_format != LCD_PIXEL_FORMAT_RGB565 ||
        (LTDC->GCR & LTDC_GCR_LTDCEN) == 0U) {
        tm_printf(reinterpret_cast<const UB *>(
                      "lcd: init verify failed x=%u y=%u format=%u gcr=%x\n"),
                  static_cast<unsigned int>(width),
                  static_cast<unsigned int>(height),
                  static_cast<unsigned int>(pixel_format),
                  static_cast<unsigned int>(LTDC->GCR));
        return DriverStatus::kHardwareError;
    }

    tm_printf(reinterpret_cast<const UB *>(
                  "lcd: init verified x=%u y=%u format=%u gcr=%x\n"),
              static_cast<unsigned int>(width),
              static_cast<unsigned int>(height),
              static_cast<unsigned int>(pixel_format),
              static_cast<unsigned int>(LTDC->GCR));
    tm_printf(reinterpret_cast<const UB *>(
                  "lcd: layer cfg sscr=%x bpcr=%x awcr=%x twcr=%x cr=%x pfcr=%x cfblr=%x cfblnr=%x cfbar=%x whpcr=%x wvpcr=%x\n"),
              static_cast<unsigned int>(LTDC->SSCR),
              static_cast<unsigned int>(LTDC->BPCR),
              static_cast<unsigned int>(LTDC->AWCR),
              static_cast<unsigned int>(LTDC->TWCR),
              static_cast<unsigned int>(LTDC_Layer1->CR),
              static_cast<unsigned int>(LTDC_Layer1->PFCR),
              static_cast<unsigned int>(LTDC_Layer1->CFBLR),
              static_cast<unsigned int>(LTDC_Layer1->CFBLNR),
              static_cast<unsigned int>(LTDC_Layer1->CFBAR),
              static_cast<unsigned int>(LTDC_Layer1->WHPCR),
              static_cast<unsigned int>(LTDC_Layer1->WVPCR));

    /* Do not expose the capture buffer until the camera has completed a
     * frame. The LTDC background color is black while the layer is hidden. */
    if (BSP_LCD_SetLayerVisible(0, 0, DISABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    return DriverStatus::kOk;
}

DriverStatus SynchronizeDisplay()
{
    return WaitForPendingDisplayFrame();
}

DriverStatus ProcessDisplay()
{
    const DriverStatus wait_status = WaitForPendingDisplayFrame();
    if (!IsOk(wait_status)) {
        return wait_status;
    }

    const std::uintptr_t frame = TakeCompletedCameraFrame();
    if (frame == 0) {
        return DriverStatus::kOk;
    }

    /* Stage the address and visibility change, then apply both at the next
     * vertical blanking period so LTDC never scans a partially switched frame. */
    if (BSP_LCD_Reload(0, BSP_LCD_RELOAD_NONE) != BSP_ERROR_NONE ||
        BSP_LCD_SetLayerAddress(0, 0, static_cast<uint32_t>(frame)) !=
            BSP_ERROR_NONE ||
        BSP_LCD_SetLayerVisible(0, 0, ENABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    ClearReloadFlag();
    if (BSP_LCD_Reload(0, BSP_LCD_RELOAD_VERTICAL_BLANKING) !=
        BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    pending_frame_address = frame;

    return DriverStatus::kOk;
}

DriverStatus ProcessDisplay(std::uintptr_t frame)
{
    if (frame == 0U) {
        return DriverStatus::kHardwareError;
    }

    const DriverStatus wait_status = WaitForPendingDisplayFrame();
    if (!IsOk(wait_status)) {
        return wait_status;
    }

    if (BSP_LCD_Reload(0, BSP_LCD_RELOAD_NONE) != BSP_ERROR_NONE ||
        BSP_LCD_SetLayerAddress(0, 0, static_cast<uint32_t>(frame)) !=
            BSP_ERROR_NONE ||
        BSP_LCD_SetLayerVisible(0, 0, ENABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }

    ClearReloadFlag();
    if (BSP_LCD_Reload(0, BSP_LCD_RELOAD_VERTICAL_BLANKING) !=
        BSP_ERROR_NONE) {
        return DriverStatus::kHardwareError;
    }
    pending_frame_address = frame;
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
    /* Match the BSP/reference LCD pixel clock: PLL4 is 50 MHz, divided by 2
     * for the panel's 25 MHz LTDC clock. PLL1/48 is only about 16.7 MHz. */
    clock.ICSelection[RCC_IC16].ClockSelection = RCC_ICCLKSOURCE_PLL4;
    clock.ICSelection[RCC_IC16].ClockDivider = 2;
    return HAL_RCCEx_PeriphCLKConfig(&clock);
}
