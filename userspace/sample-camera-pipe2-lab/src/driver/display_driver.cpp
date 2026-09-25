#include "driver/display_driver.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"
}

#include "driver/frame_buffer.hpp"

namespace uai::camera_pipe2::driver {

namespace {

/*
 * Keep the two camera buffers independent, but feed the LCD through one
 * full-width framebuffer.  This avoids relying on LTDC layer 1's window
 * address/pitch behavior while preserving the two-pipe side-by-side view.
 */
alignas(64) std::uint8_t g_display_frame_buffer[kDisplayFrameBytes];

void ComposeDisplayFrame()
{
    auto *destination = g_display_frame_buffer;
    auto *main_source = MainPipeFrameBuffer();

    SCB_InvalidateDCache_by_Addr(
        main_source, static_cast<std::int32_t>(kFrameBytes));
#if PIPE2_LAB_PIPE_DUAL
    auto *ancillary_source = AncillaryPipeFrameBuffer();
    SCB_InvalidateDCache_by_Addr(
        ancillary_source, static_cast<std::int32_t>(kFrameBytes));
#endif

    for (std::size_t y = 0U; y < kFrameHeight; ++y) {
        const auto source_offset = y * kFrameBytesPerLine;
        const auto destination_offset = y * kDisplayBytesPerLine;
        std::memcpy(destination + destination_offset,
                    main_source + source_offset, kFrameBytesPerLine);
#if PIPE2_LAB_PIPE_DUAL
        std::memcpy(destination + destination_offset + kFrameBytesPerLine,
                    ancillary_source + source_offset, kFrameBytesPerLine);
#endif
    }

    SCB_CleanDCache_by_Addr(
        g_display_frame_buffer, static_cast<std::int32_t>(kDisplayFrameBytes));
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

    BSP_LCD_LayerConfig_t display_layer{};
    display_layer.Address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(g_display_frame_buffer));
    display_layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    display_layer.X0 = 0U;
    display_layer.X1 = kDisplayWidth;
    display_layer.Y0 = 0U;
    display_layer.Y1 = kFrameHeight;

    if (BSP_LCD_ConfigLayer(0U, 0U, &display_layer) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_SetLayerVisible(0U, 0U, ENABLE) != BSP_ERROR_NONE ||
        BSP_LCD_SetLayerVisible(0U, 1U, DISABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_SetActiveLayer(0U, 0U) != BSP_ERROR_NONE ||
        BSP_LCD_DisplayOn(0U) != BSP_ERROR_NONE) {
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

    ComposeDisplayFrame();
    return DriverStatus::kOk;
}

} // namespace uai::camera_pipe2::driver
