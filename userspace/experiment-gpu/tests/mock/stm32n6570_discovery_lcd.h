#pragma once
#include "stm32n6xx_hal.h"

enum {
    BSP_ERROR_NONE = 0,
    LCD_PIXEL_FORMAT_RGB565,
    ENABLE,
    DISABLE
};

struct MX_LTDC_LayerConfig_t {
    std::uint32_t X0, X1, Y0, Y1, PixelFormat, Address;
    std::uintptr_t FBStartAdress = 0;
};
using BSP_LCD_LayerConfig_t = MX_LTDC_LayerConfig_t;
struct LTDC_HandleTypeDef {
    MX_LTDC_LayerConfig_t LayerCfg[2]{};
};

extern "C" {
inline LTDC_HandleTypeDef hlcd_ltdc{};
}

inline std::int32_t BSP_LCD_ConfigLayer(
    std::uint32_t,
    std::uint32_t,
    BSP_LCD_LayerConfig_t *
)
{
    return BSP_ERROR_NONE;
}

inline std::int32_t BSP_LCD_SetLayerVisible(
    std::uint32_t,
    std::uint32_t,
    std::uint32_t
)
{
    return BSP_ERROR_NONE;
}

inline HAL_StatusTypeDef HAL_LTDC_ConfigColorKeying(
    LTDC_HandleTypeDef *,
    std::uint32_t,
    std::uint32_t
)
{
    return HAL_OK;
}

inline HAL_StatusTypeDef HAL_LTDC_EnableColorKeying(
    LTDC_HandleTypeDef *,
    std::uint32_t
)
{
    return HAL_OK;
}
