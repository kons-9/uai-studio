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

/* Keep one LTDC scanout buffer: the selected camera fills the left half and
 * the experiment UI fills the right half. This avoids depending on LTDC
 * layer 1 window and external-memory fetch behavior for the UI. */
alignas(64) std::uint8_t g_display_frame_buffer[kDisplayFrameBytes];
std::uint16_t *g_ui_panel_buffer = nullptr;
bool g_display_pipe2 = false;
bool g_result_visible = false;
std::uint32_t g_result_x = 0U, g_result_y = 0U, g_result_width = 0U, g_result_height = 0U;
#if defined(EXPERIMENT_GPU_VISUAL)
bool g_visual_overlay_preserved = false;
constexpr std::size_t kStatusLabelWidth = 432U;
constexpr std::size_t kStatusLabelHeight = 48U;
constexpr std::size_t kVisualPanelX = 480U;
constexpr std::size_t kVisualPanelY = 32U;
constexpr std::size_t kVisualPanelWidth = 320U;
constexpr std::size_t kVisualPanelHeight = 112U;

void CopyDisplaySegment(
    std::uint8_t *destination,
    const std::uint8_t *main_source,
    const std::uint8_t *ancillary_source,
    std::size_t x,
    std::size_t pixels
)
{
#if PIPE2_PIPE_DUAL
    if (x < kFrameWidth) {
        const auto main_pixels = pixels < kFrameWidth - x ? pixels : kFrameWidth - x;
        std::memcpy(destination + x * 2U, main_source + x * 2U, main_pixels * 2U);
        x += main_pixels;
        pixels -= main_pixels;
    }
    if (pixels != 0U) {
        const auto ancillary_x = x - kFrameWidth;
        std::memcpy(destination + x * 2U, ancillary_source + ancillary_x * 2U, pixels * 2U);
    }
#else
    (void)ancillary_source;
    std::memcpy(destination + x * 2U, main_source + x * 2U, pixels * 2U);
#endif
}

void ComposeVisualRow(
    std::uint8_t *destination,
    const std::uint8_t *main_source,
    const std::uint8_t *ancillary_source,
    std::size_t y
)
{
    std::size_t x = 0U;
    const auto copy_until = [&](std::size_t end) {
        if (end > x) {
            CopyDisplaySegment(destination, main_source, ancillary_source, x, end - x);
            x = end;
        }
    };

    if (y < kStatusLabelHeight) {
        x = kStatusLabelWidth;
    }
    if (y >= kVisualPanelY && y < kVisualPanelY + kVisualPanelHeight) {
        copy_until(kVisualPanelX);
        x = kDisplayWidth;
    }
    copy_until(kDisplayWidth);
}
#endif

void ComposeDisplayFrame()
{
    auto *destination = g_display_frame_buffer;
    auto *camera_source = g_display_pipe2 ? AncillaryPipeFrameBuffer() : MainPipeFrameBuffer();
    SCB_InvalidateDCache_by_Addr(camera_source, static_cast<std::int32_t>(kFrameBytes));

    for (std::size_t y = 0U; y < kFrameHeight; ++y) {
        const auto source_offset = y * kFrameBytesPerLine;
        const auto destination_offset = y * kDisplayBytesPerLine;
#if PIPE2_PIPE_DUAL
        std::memcpy(destination + destination_offset, camera_source + source_offset, kFrameBytesPerLine);
        auto *panel_row = destination + destination_offset + kFrameBytesPerLine;
        if (g_ui_panel_buffer != nullptr) {
            std::memcpy(
                panel_row,
                g_ui_panel_buffer + y * kFrameWidth,
                kFrameBytesPerLine
            );
        } else {
            std::memset(panel_row, 0, kFrameBytesPerLine);
        }
#else
#if defined(EXPERIMENT_GPU_VISUAL)
        if (g_visual_overlay_preserved) {
            ComposeVisualRow(
                destination + destination_offset,
                camera_source + source_offset,
                nullptr,
                y
            );
        } else {
            CopyDisplaySegment(
                destination + destination_offset,
                camera_source + source_offset,
                nullptr,
                0U,
                kDisplayWidth
            );
        }
#else
        std::memcpy(destination + destination_offset, camera_source + source_offset, kFrameBytesPerLine);
#endif
#endif
    }

#if PIPE2_PIPE_DUAL
    if (g_result_visible && g_result_width != 0U && g_result_height != 0U
        && g_result_x < kFrameWidth && g_result_width <= kFrameWidth - g_result_x
        && g_result_y < kFrameHeight && g_result_height <= kFrameHeight - g_result_y) {
        auto *display_pixels = reinterpret_cast<std::uint16_t *>(destination);
        const auto right = g_result_x + g_result_width - 1U;
        const auto bottom = g_result_y + g_result_height - 1U;
        for (std::uint32_t x = g_result_x; x <= right; ++x) {
            display_pixels[g_result_y * kDisplayWidth + x] = 0xffffU;
            display_pixels[bottom * kDisplayWidth + x] = 0xffffU;
        }
        for (std::uint32_t y = g_result_y; y <= bottom; ++y) {
            display_pixels[y * kDisplayWidth + g_result_x] = 0xffffU;
            display_pixels[y * kDisplayWidth + right] = 0xffffU;
        }
    }
#endif

    SCB_CleanDCache_by_Addr(g_display_frame_buffer, static_cast<std::int32_t>(kDisplayFrameBytes));
}

} // namespace

void SetVisualOverlayPreserved(bool preserve)
{
#if defined(EXPERIMENT_GPU_VISUAL)
    g_visual_overlay_preserved = preserve;
#else
    (void)preserve;
#endif
}

bool SetDisplayPipe2(bool pipe2)
{
#if PIPE2_PIPE_DUAL
    g_display_pipe2 = pipe2;
    return true;
#else
    return !pipe2;
#endif
}

bool SetUiPanelBuffer(std::uint16_t *pixels)
{
#if PIPE2_PIPE_DUAL
    g_ui_panel_buffer = pixels;
    return pixels != nullptr;
#else
    (void)pixels;
    return false;
#endif
}

void SetResultOverlay(
    bool visible,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height
)
{
    g_result_visible = visible;
    g_result_x = x;
    g_result_y = y;
    g_result_width = width;
    g_result_height = height;
}

DriverStatus DisplayDriver::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }

    if (BSP_LCD_Init(0U, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    BSP_LCD_LayerConfig_t display_layer{};
    display_layer.Address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_display_frame_buffer));
    display_layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    display_layer.X0 = 0U;
    display_layer.X1 = kDisplayWidth;
    display_layer.Y0 = 0U;
    display_layer.Y1 = kFrameHeight;

    if (BSP_LCD_ConfigLayer(0U, 0U, &display_layer) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_SetLayerVisible(0U, 0U, ENABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(0U, 1U, DISABLE) != BSP_ERROR_NONE) {
        return DriverStatus::kHardwareFailure;
    }

    if (BSP_LCD_SetActiveLayer(0U, 0U) != BSP_ERROR_NONE || BSP_LCD_DisplayOn(0U) != BSP_ERROR_NONE) {
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
