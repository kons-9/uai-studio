#include "display_log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" {
#include "fonts.h"
#include "stm32n6xx_hal.h"
#include "stm32n6570_discovery_lcd.h"

alignas(32) std::uint16_t experiment_hwtest_display_framebuffer[800U * 480U]{};
}

namespace experiment::hwtest::display_log {
namespace {

constexpr std::uint32_t kWidth = 800U;
constexpr std::uint32_t kHeight = 480U;
constexpr std::uint32_t kMargin = 16U;
constexpr std::uint32_t kLogTop = 40U;
constexpr std::uint32_t kLineHeight = 16U;
constexpr std::uint32_t kBottomMargin = 8U;
constexpr std::uint16_t kBackground = 0x0000U;
constexpr std::uint16_t kWhite = 0xffffU;
constexpr std::uint16_t kCyan = 0x07ffU;
constexpr std::uint16_t kYellow = 0xffe0U;
constexpr std::uint16_t kGreen = 0x07e0U;
constexpr std::uint16_t kRed = 0xf800U;

bool initialized = false;
bool ready = false;
std::uint32_t cursor_x = kMargin;
std::uint32_t cursor_y = kLogTop;

void ConfigureSecurity()
{
    __HAL_RCC_RIFSC_CLK_ENABLE();

    RIMC_MasterConfig_t media_master{};
    media_master.MasterCID = RIF_CID_1;
    media_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC2, &media_master);
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &media_master);

    constexpr std::uint32_t secure_privileged = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    constexpr std::uint32_t peripherals[] = {
        RIF_RISC_PERIPH_INDEX_LTDC,
        RIF_RISC_PERIPH_INDEX_LTDCL1,
        RIF_RISC_PERIPH_INDEX_LTDCL2,
        RIF_RISC_PERIPH_INDEX_DMA2D,
        RIF_RCC_PERIPH_INDEX_CACHEAXIRAM,
        RIF_RCC_PERIPH_INDEX_CACHECONFIG,
        RIF_RCC_PERIPH_INDEX_AXISRAM1,
        RIF_RCC_PERIPH_INDEX_AXISRAM2,
        RIF_RCC_PERIPH_INDEX_FLEXRAM,
    };
    for (const std::uint32_t peripheral : peripherals) {
        HAL_RIF_RISC_SetSlaveSecureAttributes(peripheral, secure_privileged);
    }
}

void CleanFramebuffer()
{
    SCB_CleanDCache_by_Addr(
        reinterpret_cast<std::uint32_t *>(experiment_hwtest_display_framebuffer),
        static_cast<std::int32_t>(sizeof(experiment_hwtest_display_framebuffer))
    );
    __DSB();
}

void DrawCharacter(std::uint32_t x, std::uint32_t y, unsigned char character, std::uint16_t color)
{
    if (character < ' ' || character > '~') {
        character = '?';
    }

    const std::uint32_t bytes_per_row = (Font12.Width + 7U) / 8U;
    const std::uint32_t glyph_offset =
        (static_cast<std::uint32_t>(character) - static_cast<std::uint32_t>(' ')) * Font12.Height * bytes_per_row;
    const auto *glyph = Font12.table + glyph_offset;
    const std::uint32_t bit_offset = bytes_per_row * 8U - Font12.Width;

    for (std::uint32_t row = 0; row < Font12.Height; ++row) {
        const std::uint32_t row_bits = glyph[row * bytes_per_row];
        for (std::uint32_t column = 0; column < Font12.Width; ++column) {
            const std::uint32_t bit = Font12.Width - column + bit_offset - 1U;
            if ((row_bits & (1U << bit)) != 0U) {
                experiment_hwtest_display_framebuffer[(y + row) * kWidth + x + column] = color;
            }
        }
    }
}

void ScrollOneLine()
{
    const std::uint32_t visible_end = kHeight - kBottomMargin;
    const std::uint32_t source_y = kLogTop + kLineHeight;
    const std::uint32_t rows_to_move = visible_end - source_y;
    const std::size_t row_bytes = kWidth * sizeof(experiment_hwtest_display_framebuffer[0]);
    std::memmove(
        experiment_hwtest_display_framebuffer + kLogTop * kWidth,
        experiment_hwtest_display_framebuffer + source_y * kWidth,
        static_cast<std::size_t>(rows_to_move) * row_bytes
    );
    std::fill(
        experiment_hwtest_display_framebuffer + (kLogTop + rows_to_move) * kWidth,
        experiment_hwtest_display_framebuffer + kHeight * kWidth,
        kBackground
    );
    cursor_y = kHeight - kBottomMargin - Font12.Height;
}

void AdvanceLine()
{
    cursor_x = kMargin;
    cursor_y += kLineHeight;
    const std::uint32_t last_line_y = kHeight - kBottomMargin - Font12.Height;
    if (cursor_y > last_line_y) {
        ScrollOneLine();
    }
}

std::uint16_t ColorFor(const char *text)
{
    if (std::strstr(text, " START") != nullptr) {
        return kYellow;
    }
    if (std::strstr(text, " PASS") != nullptr) {
        return kGreen;
    }
    if (std::strstr(text, " FAIL") != nullptr) {
        return kRed;
    }
    return kWhite;
}

} // namespace

bool Initialize()
{
    if (initialized) {
        return ready;
    }
    initialized = true;
    ConfigureSecurity();

    if (BSP_LCD_Init(0U, LCD_ORIENTATION_LANDSCAPE) != BSP_ERROR_NONE) {
        return false;
    }

    BSP_LCD_LayerConfig_t layer{};
    layer.Address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(experiment_hwtest_display_framebuffer));
    layer.PixelFormat = LCD_PIXEL_FORMAT_RGB565;
    layer.X0 = 0U;
    layer.X1 = kWidth;
    layer.Y0 = 0U;
    layer.Y1 = kHeight;
    if (BSP_LCD_ConfigLayer(0U, 0U, &layer) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(0U, 1U, DISABLE) != BSP_ERROR_NONE
        || BSP_LCD_SetActiveLayer(0U, 0U) != BSP_ERROR_NONE
        || BSP_LCD_SetLayerVisible(0U, 0U, ENABLE) != BSP_ERROR_NONE
        || BSP_LCD_DisplayOn(0U) != BSP_ERROR_NONE) {
        return false;
    }

    std::fill(
        experiment_hwtest_display_framebuffer,
        experiment_hwtest_display_framebuffer + kWidth * kHeight,
        kBackground
    );
    cursor_x = kMargin;
    cursor_y = kLogTop;
    const char title[] = "EXPERIMENT HW TEST";
    for (std::uint32_t index = 0; title[index] != '\0'; ++index) {
        DrawCharacter(kMargin + index * (Font12.Width + 1U), 10U, static_cast<unsigned char>(title[index]), kCyan);
    }
    CleanFramebuffer();
    if (BSP_LCD_Reload(0U, BSP_LCD_RELOAD_IMMEDIATE) != BSP_ERROR_NONE) {
        return false;
    }
    ready = true;
    return true;
}

bool Ready()
{
    return ready;
}

bool SelfTest()
{
    if (!ready) {
        return false;
    }
    constexpr std::size_t probe = kWidth * kHeight - 1U;
    const std::uint16_t previous = experiment_hwtest_display_framebuffer[probe];
    experiment_hwtest_display_framebuffer[probe] = kCyan;
    const bool passed = experiment_hwtest_display_framebuffer[probe] == kCyan;
    experiment_hwtest_display_framebuffer[probe] = previous;
    CleanFramebuffer();
    return passed;
}

void Write(const char *text)
{
    if (!ready || text == nullptr) {
        return;
    }
    const std::uint16_t color = ColorFor(text);
    for (const auto *character = reinterpret_cast<const unsigned char *>(text); *character != '\0'; ++character) {
        if (*character == '\r') {
            continue;
        }
        if (*character == '\n') {
            AdvanceLine();
            continue;
        }
        if (cursor_x + Font12.Width > kWidth - kMargin) {
            AdvanceLine();
        }
        DrawCharacter(cursor_x, cursor_y, *character, color);
        cursor_x += Font12.Width + 1U;
    }
    CleanFramebuffer();
}

} // namespace experiment::hwtest::display_log
