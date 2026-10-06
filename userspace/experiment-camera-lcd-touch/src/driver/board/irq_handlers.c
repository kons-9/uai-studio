#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;
extern LTDC_HandleTypeDef hlcd_ltdc;

volatile uint32_t camera_lcd_touch_ltdc_reload_count;

void CSI_IRQHandler(void)
{
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);
}

void DCMIPP_IRQHandler(void)
{
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}

void LTDC_UP_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hlcd_ltdc);
}

void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc)
{
    UNUSED(hltdc);
    ++camera_lcd_touch_ltdc_reload_count;
}
