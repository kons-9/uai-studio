#include "middleware/ui/canvas.hpp"

namespace uai::ai::ui {

namespace {

struct Glyph {
    char letter;
    std::uint8_t rows[kGlyphHeight];
};

/* Bit 4 is the leftmost column. Lowercase letters map to uppercase. */
constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},
    {':', {0, 4, 0, 0, 0, 4, 0}},
    {'/', {0, 1, 2, 4, 8, 16, 0}},
    {'%', {24, 25, 2, 4, 8, 19, 3}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {31, 2, 4, 2, 1, 17, 14}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 30, 1, 1, 17, 14}},
    {'6', {6, 8, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 2, 12}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},
    {'J', {7, 2, 2, 2, 2, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 17, 25, 21, 19, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 17, 10, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
};

} // namespace

std::uint8_t GlyphRow(char letter, std::uint8_t row)
{
    if (row >= kGlyphHeight) {
        return 0U;
    }
    if (letter >= 'a' && letter <= 'z') {
        letter = static_cast<char>(letter - 'a' + 'A');
    }
    for (const Glyph &glyph : kGlyphs) {
        if (glyph.letter == letter) {
            return glyph.rows[row];
        }
    }
    return 0U;
}

void Canvas::PutPixel(std::uint16_t x, std::uint16_t y, std::uint16_t color)
{
    if (pixels_ != nullptr && x < width_ && y < height_) {
        pixels_[static_cast<std::size_t>(y) * width_ + x] = color;
    }
}

void Canvas::FillRect(const Rect &rect, std::uint16_t color)
{
    if (pixels_ == nullptr || rect.x >= width_ || rect.y >= height_) {
        return;
    }
    const std::uint32_t x_end = static_cast<std::uint32_t>(rect.x) + rect.width;
    const std::uint32_t y_end = static_cast<std::uint32_t>(rect.y) + rect.height;
    const std::uint32_t clipped_x_end = x_end < width_ ? x_end : width_;
    const std::uint32_t clipped_y_end = y_end < height_ ? y_end : height_;
    for (std::uint32_t row = rect.y; row < clipped_y_end; ++row) {
        std::uint16_t *line = pixels_ + static_cast<std::size_t>(row) * width_;
        for (std::uint32_t column = rect.x; column < clipped_x_end; ++column) {
            line[column] = color;
        }
    }
}

void Canvas::DrawFrame(const Rect &rect, std::uint16_t thickness,
                       std::uint16_t color)
{
    if (thickness == 0U || rect.width == 0U || rect.height == 0U) {
        return;
    }
    const std::uint16_t t_x = thickness < rect.width ? thickness : rect.width;
    const std::uint16_t t_y = thickness < rect.height ? thickness : rect.height;
    FillRect({rect.x, rect.y, rect.width, t_y}, color);
    FillRect({rect.x, static_cast<std::uint16_t>(rect.y + rect.height - t_y),
              rect.width, t_y}, color);
    FillRect({rect.x, rect.y, t_x, rect.height}, color);
    FillRect({static_cast<std::uint16_t>(rect.x + rect.width - t_x), rect.y,
              t_x, rect.height}, color);
}

void Canvas::DrawText(std::uint16_t x, std::uint16_t y, const char *text,
                      std::uint8_t scale, std::uint16_t color)
{
    if (text == nullptr || scale == 0U) {
        return;
    }
    std::uint32_t pen_x = x;
    for (const char *letter = text; *letter != '\0'; ++letter) {
        for (std::uint8_t row = 0U; row < kGlyphHeight; ++row) {
            const std::uint8_t bits = GlyphRow(*letter, row);
            for (std::uint8_t column = 0U; column < kGlyphWidth; ++column) {
                if ((bits & (1U << (kGlyphWidth - 1U - column))) == 0U) {
                    continue;
                }
                const std::uint32_t px = pen_x + column * scale;
                const std::uint32_t py = y + static_cast<std::uint32_t>(row) * scale;
                if (px >= width_ || py >= height_) {
                    continue;
                }
                FillRect({static_cast<std::uint16_t>(px),
                          static_cast<std::uint16_t>(py), scale, scale}, color);
            }
        }
        pen_x += static_cast<std::uint32_t>(kGlyphAdvance) * scale;
    }
}

void Canvas::DrawTextCentered(const Rect &rect, const char *text,
                              std::uint8_t scale, std::uint16_t color)
{
    const std::uint32_t text_width = TextWidth(text, scale);
    const std::uint32_t text_height = static_cast<std::uint32_t>(kGlyphHeight) * scale;
    const std::uint32_t x = text_width < rect.width
        ? rect.x + (rect.width - text_width) / 2U
        : rect.x;
    const std::uint32_t y = text_height < rect.height
        ? rect.y + (rect.height - text_height) / 2U
        : rect.y;
    DrawText(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
             text, scale, color);
}

} // namespace uai::ai::ui
