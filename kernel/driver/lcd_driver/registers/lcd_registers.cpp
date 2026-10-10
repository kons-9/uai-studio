#include "driver/lcd_driver/registers/lcd_registers.hpp"
#include "driver/lcd_driver/display_state.hpp"

#include <initializer_list>

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
std::uintptr_t uai_lcd_initial_framebuffer = 0;
}

namespace uai::ai::lcd {

DisplayState ReadDisplayState()
{
    return {
        (LTDC->GCR & LTDC_GCR_LTDCEN) != 0U,
        (LTDC_Layer1->CR & LTDC_LxCR_LEN) != 0U,
        LTDC_Layer1->PFCR == LTDC_PIXEL_FORMAT_RGB565,
        LTDC_Layer1->CFBAR,
        LTDC_Layer1->CFBLNR & LTDC_LxCFBLNR_CFBLNBR
    };
}

}

namespace uai::ai::lcd::registers {
namespace {

constexpr std::uint32_t kDisplayInstance = 0U;
constexpr std::uint32_t kDisplayLayer = 0U;
constexpr std::uint32_t kDisplayWidth = 800U;
constexpr std::uint32_t kDisplayHeight = 480U;

} // namespace

uai::driver::DriverStatus LcdRegisterLayer::Initialize(std::uintptr_t initial_buffer)
{
    if (initialized_) {
        return uai::driver::DriverStatus::kAlreadyInitialized;
    }
    __HAL_RCC_RIFSC_CLK_ENABLE();
    RIMC_MasterConfig_t master{};
    master.MasterCID = RIF_CID_1;
    master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &master);
    for (const auto peripheral :
         {RIF_RISC_PERIPH_INDEX_LTDC,
          RIF_RISC_PERIPH_INDEX_LTDCL1,
          RIF_RISC_PERIPH_INDEX_LTDCL2,
          RIF_RCC_PERIPH_INDEX_CACHEAXIRAM,
          RIF_RCC_PERIPH_INDEX_CACHECONFIG,
          RIF_RCC_PERIPH_INDEX_AXISRAM1,
          RIF_RCC_PERIPH_INDEX_AXISRAM2,
          RIF_RCC_PERIPH_INDEX_FLEXRAM}) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
    }
    uai_lcd_initial_framebuffer = initial_buffer;
    if (BSP_LCD_Init(kDisplayInstance, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return uai::driver::DriverStatus::kHardwareError;
    }

    BSP_LCD_LayerConfig_t layer{};
    layer.Address = static_cast<std::uint32_t>(initial_buffer);
    layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    layer.X0 = 0U;
    layer.X1 = kDisplayWidth;
    layer.Y0 = 0U;
    layer.Y1 = kDisplayHeight;
    if (BSP_LCD_ConfigLayer(kDisplayInstance, kDisplayLayer, &layer) != BSP_ERROR_NONE) {
        (void)BSP_LCD_DeInit(kDisplayInstance);
        return uai::driver::DriverStatus::kHardwareError;
    }
    if (BSP_LCD_SetLayerVisible(kDisplayInstance, kDisplayLayer, DISABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(kDisplayInstance, 1U, DISABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetActiveLayer(kDisplayInstance, kDisplayLayer) != BSP_ERROR_NONE
        || BSP_LCD_DisplayOn(kDisplayInstance) != BSP_ERROR_NONE) {
        (void)BSP_LCD_DeInit(kDisplayInstance);
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
    if ((LTDC->SRCR & LTDC_SRCR_VBR) != 0U) {
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
    if (buffer == 0U)
        return uai::driver::DriverStatus::kHardwareError;
    __HAL_LTDC_CLEAR_FLAG(&hlcd_ltdc, LTDC_FLAG_RR);
    if (HAL_LTDC_SetAddress_NoReload(&hlcd_ltdc, static_cast<std::uint32_t>(buffer), LTDC_LAYER_1) != HAL_OK)
        return uai::driver::DriverStatus::kHardwareError;
    __HAL_LTDC_LAYER_ENABLE(&hlcd_ltdc, LTDC_LAYER_1);
    if (HAL_LTDC_Reload(&hlcd_ltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) {
        return uai::driver::DriverStatus::kHardwareError;
    }
    reload_pending_ = true;
    return uai::driver::DriverStatus::kOk;
}

} // namespace uai::ai::lcd::registers
