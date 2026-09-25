#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_camera.h"

extern DCMIPP_HandleTypeDef hcamera_dcmipp;

void CSI_IRQHandler(void)
{
    HAL_DCMIPP_CSI_IRQHandler(&hcamera_dcmipp);
}

void DCMIPP_IRQHandler(void)
{
    HAL_DCMIPP_IRQHandler(&hcamera_dcmipp);
}
