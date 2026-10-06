#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::ui {

struct Rect {
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;

    constexpr bool Contains(std::uint16_t px, std::uint16_t py) const
    {
        return px >= x && py >= y &&
               static_cast<std::uint32_t>(px) < static_cast<std::uint32_t>(x) + width &&
               static_cast<std::uint32_t>(py) < static_cast<std::uint32_t>(y) + height;
    }
};

constexpr std::uint16_t Rgb565(std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue)
{
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) |
                                      ((green & 0xFCU) << 3U) |
                                      (blue >> 3U));
}

/* Built-in 5x7 glyphs; the host renderer in host_app/ui_designer mirrors
 * this table so the preview and the device output are pixel identical. */
inline constexpr std::uint8_t kGlyphWidth = 5U;
inline constexpr std::uint8_t kGlyphHeight = 7U;
inline constexpr std::uint8_t kGlyphAdvance = 6U;

std::uint8_t GlyphRow(char letter, std::uint8_t row);

constexpr std::uint32_t TextWidth(const char *text, std::uint8_t scale)
{
    std::uint32_t length = 0U;
    while (text != nullptr && text[length] != '\0') {
        ++length;
    }
    if (length == 0U) {
        return 0U;
    }
    return (length * kGlyphAdvance - (kGlyphAdvance - kGlyphWidth)) * scale;
}

/* Non-owning view over an RGB565 frame. Every operation clips to bounds and
 * performs no cache maintenance. */
class Canvas final {
public:
    Canvas(std::uint16_t *pixels, std::uint16_t width, std::uint16_t height)
        : pixels_(pixels), width_(width), height_(height) {}

    std::uint16_t Width() const { return width_; }
    std::uint16_t Height() const { return height_; }

    void PutPixel(std::uint16_t x, std::uint16_t y, std::uint16_t color);
    void FillRect(const Rect &rect, std::uint16_t color);
    void DrawFrame(const Rect &rect, std::uint16_t thickness,
                   std::uint16_t color);
    void DrawText(std::uint16_t x, std::uint16_t y, const char *text,
                  std::uint8_t scale, std::uint16_t color);
    void DrawTextCentered(const Rect &rect, const char *text,
                          std::uint8_t scale, std::uint16_t color);

private:
    std::uint16_t *pixels_;
    std::uint16_t width_;
    std::uint16_t height_;
};

} // namespace uai::ai::ui
