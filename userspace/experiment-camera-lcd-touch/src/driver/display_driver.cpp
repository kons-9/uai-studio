#include "driver/display_driver.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
extern LTDC_HandleTypeDef hlcd_ltdc;
extern volatile std::uint32_t camera_lcd_touch_ltdc_reload_count;
}

#include "driver/frame_buffer.hpp"

namespace uai::camera_lcd_touch::driver {

namespace {

void ComposeDisplayFrame(OverlayUi &overlay_ui, std::uint16_t *display)
{
    auto *capture = FrameBuffer();
    SCB_InvalidateDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(capture),
        static_cast<std::int32_t>(kFrameBytes));
    std::memcpy(display, capture, kFrameBytes);
    overlay_ui.DrawOn(display);
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(display),
        static_cast<std::int32_t>(kFrameBytes));
}

} // namespace

DriverStatus DisplayDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    if (BSP_LCD_Init(0U, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }
    HAL_NVIC_SetPriority(LTDC_UP_IRQn, 15U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_UP_IRQn);

    BSP_LCD_LayerConfig_t camera_layer{};
    camera_layer.Address = static_cast<std::uint32_t>(
        DisplayFrameBufferAddress(front_buffer_index_));
    camera_layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    camera_layer.X0 = 0U;
    camera_layer.X1 = 800U;
    camera_layer.Y0 = 0U;
    camera_layer.Y1 = 480U;

    if (BSP_LCD_ConfigLayer(0U, 0U, &camera_layer) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (!IsOk(overlay_ui_.Initialize())) {
        return DriverStatus::kHardwareFailure;
    }

    /* Fill both scanout pages before the LTDC starts reading either one. */
    ComposeDisplayFrame(overlay_ui_, DisplayFrameBuffer(0U));
    ComposeDisplayFrame(overlay_ui_, DisplayFrameBuffer(1U));

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
    if (!initialized_) {
        return DriverStatus::kNotInitialized;
    }

    if (flip_pending_) {
        if (camera_lcd_touch_ltdc_reload_count == pending_reload_count_) {
            return DriverStatus::kOk;
        }
        front_buffer_index_ = pending_buffer_index_;
        flip_pending_ = false;
    }

    const auto back_buffer_index =
        static_cast<std::uint8_t>(1U - front_buffer_index_);
    ComposeDisplayFrame(overlay_ui_, DisplayFrameBuffer(back_buffer_index));
    pending_reload_count_ = camera_lcd_touch_ltdc_reload_count + 1U;
    if (HAL_LTDC_SetAddress_NoReload(
            &hlcd_ltdc,
            static_cast<std::uint32_t>(
                DisplayFrameBufferAddress(back_buffer_index)),
            LTDC_LAYER_1) != HAL_OK ||
        HAL_LTDC_Reload(&hlcd_ltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) {
        return DriverStatus::kHardwareFailure;
    }
    pending_buffer_index_ = back_buffer_index;
    flip_pending_ = true;
    return DriverStatus::kOk;
}

TouchAction DisplayDriver::HandleTouchPress(std::uint16_t x, std::uint16_t y)
{
    return overlay_ui_.PressAt(x, y);
}

void DisplayDriver::HandleTouchRelease()
{
    overlay_ui_.Release();
}

} // namespace uai::camera_lcd_touch::driver
