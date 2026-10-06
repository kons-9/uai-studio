#include "driver/overlay_ui.hpp"

#include <cstddef>
#include <cstdint>

#include "driver/frame_buffer.hpp"

namespace uai::camera_lcd_touch::driver {

namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kYellow = 0xFFE0U;
constexpr std::uint16_t kButtonY = 392U;
constexpr std::uint16_t kButtonWidth = 186U;
constexpr std::uint16_t kButtonHeight = 72U;
constexpr std::uint16_t kPanelColor = 0x0841U;

struct Button {
    const char *label;
    TouchAction action;
    std::uint16_t color;
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t width;
    std::uint16_t height;
};

constexpr Button kButtons[] = {
    {"RED", TouchAction::kSelectRed, 0xF800U, 16U, kButtonY,
     kButtonWidth, kButtonHeight},
    {"GREEN", TouchAction::kSelectGreen, 0x07E0U, 210U, kButtonY,
     kButtonWidth, kButtonHeight},
    {"BLUE", TouchAction::kSelectBlue, 0x001FU, 404U, kButtonY,
     kButtonWidth, kButtonHeight},
    {"CLEAR", TouchAction::kClearMarker, 0x8410U, 598U, kButtonY,
     kButtonWidth, kButtonHeight},
};
constexpr std::size_t kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

std::uint8_t GlyphRow(char letter, std::uint8_t row)
{
    static constexpr std::uint8_t kA[7] = {14, 17, 17, 31, 17, 17, 17};
    static constexpr std::uint8_t kB[7] = {30, 17, 17, 30, 17, 17, 30};
    static constexpr std::uint8_t kC[7] = {14, 17, 16, 16, 16, 17, 14};
    static constexpr std::uint8_t kD[7] = {30, 17, 17, 17, 17, 17, 30};
    static constexpr std::uint8_t kE[7] = {31, 16, 16, 30, 16, 16, 31};
    static constexpr std::uint8_t kG[7] = {14, 17, 16, 23, 17, 17, 15};
    static constexpr std::uint8_t kH[7] = {17, 17, 17, 31, 17, 17, 17};
    static constexpr std::uint8_t kI[7] = {31, 4, 4, 4, 4, 4, 31};
    static constexpr std::uint8_t kL[7] = {16, 16, 16, 16, 16, 16, 31};
    static constexpr std::uint8_t kM[7] = {17, 27, 21, 21, 17, 17, 17};
    static constexpr std::uint8_t kN[7] = {17, 25, 25, 21, 19, 19, 17};
    static constexpr std::uint8_t kO[7] = {14, 17, 17, 17, 17, 17, 14};
    static constexpr std::uint8_t kP[7] = {30, 17, 17, 30, 16, 16, 16};
    static constexpr std::uint8_t kR[7] = {30, 17, 17, 30, 20, 18, 17};
    static constexpr std::uint8_t kT[7] = {31, 4, 4, 4, 4, 4, 4};
    static constexpr std::uint8_t kU[7] = {17, 17, 17, 17, 17, 17, 14};

    const std::uint8_t *glyph = nullptr;
    switch (letter) {
    case 'A': glyph = kA; break;
    case 'B': glyph = kB; break;
    case 'C': glyph = kC; break;
    case 'D': glyph = kD; break;
    case 'E': glyph = kE; break;
    case 'G': glyph = kG; break;
    case 'H': glyph = kH; break;
    case 'I': glyph = kI; break;
    case 'L': glyph = kL; break;
    case 'M': glyph = kM; break;
    case 'N': glyph = kN; break;
    case 'O': glyph = kO; break;
    case 'P': glyph = kP; break;
    case 'R': glyph = kR; break;
    case 'T': glyph = kT; break;
    case 'U': glyph = kU; break;
    default: break;
    }
    return glyph != nullptr && row < 7U ? glyph[row] : 0U;
}

void PutPixel(std::uint16_t *pixels, std::uint16_t x, std::uint16_t y,
              std::uint16_t color)
{
    if (pixels != nullptr && x < kFrameWidth && y < kFrameHeight) {
        pixels[static_cast<std::size_t>(y) * kFrameWidth + x] = color;
    }
}

void FillRect(std::uint16_t *pixels, std::uint16_t x, std::uint16_t y,
              std::uint16_t width, std::uint16_t height,
              std::uint16_t color)
{
    if (pixels == nullptr || x >= kFrameWidth || y >= kFrameHeight) {
        return;
    }
    const std::uint32_t x_end =
        (static_cast<std::uint32_t>(x) + width < kFrameWidth)
            ? static_cast<std::uint32_t>(x) + width
            : kFrameWidth;
    const std::uint32_t y_end =
        (static_cast<std::uint32_t>(y) + height < kFrameHeight)
            ? static_cast<std::uint32_t>(y) + height
            : kFrameHeight;
    for (std::uint32_t row = y; row < y_end; ++row) {
        auto *line = pixels + static_cast<std::size_t>(row) * kFrameWidth;
        for (std::uint32_t column = x; column < x_end; ++column) {
            line[column] = color;
        }
    }
}

std::uint16_t BlendNavy(std::uint16_t background)
{
    constexpr std::uint16_t kNavy = 0x0010U;
    const std::uint16_t red = static_cast<std::uint16_t>(
        (((background >> 11U) & 0x1FU) + ((kNavy >> 11U) & 0x1FU)) / 2U);
    const std::uint16_t green = static_cast<std::uint16_t>(
        (((background >> 5U) & 0x3FU) + ((kNavy >> 5U) & 0x3FU)) / 2U);
    const std::uint16_t blue = static_cast<std::uint16_t>(
        ((background & 0x1FU) + (kNavy & 0x1FU)) / 2U);
    return static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
}

void DrawText(std::uint16_t *pixels, std::uint16_t x, std::uint16_t y,
              const char *text, std::uint8_t scale, std::uint16_t color);

void DrawHeader(std::uint16_t *pixels)
{
    for (std::uint16_t y = 16U; y < 78U; ++y) {
        auto *line = pixels + static_cast<std::size_t>(y) * kFrameWidth;
        for (std::uint16_t x = 16U; x < 784U; ++x) {
            line[x] = BlendNavy(line[x]);
        }
    }
    DrawText(pixels, 32U, 31U, "TOUCH UI", 4U, kWhite);
    DrawText(pixels, 336U, 39U, "TAP IMAGE TO MARK", 2U, kWhite);
}

void DrawText(std::uint16_t *pixels, std::uint16_t x, std::uint16_t y,
              const char *text, std::uint8_t scale, std::uint16_t color)
{
    if (text == nullptr || scale == 0U) {
        return;
    }
    for (const char *letter = text; *letter != '\0'; ++letter) {
        for (std::uint8_t row = 0U; row < 7U; ++row) {
            const std::uint8_t bits = GlyphRow(*letter, row);
            for (std::uint8_t column = 0U; column < 5U; ++column) {
                if ((bits & (1U << (4U - column))) != 0U) {
                    FillRect(pixels,
                             static_cast<std::uint16_t>(x + column * scale),
                             static_cast<std::uint16_t>(y + row * scale),
                             scale, scale, color);
                }
            }
        }
        x = static_cast<std::uint16_t>(x + 7U * scale);
    }
}

void DrawButton(std::uint16_t *pixels, std::size_t index,
                std::int8_t pressed_button, std::uint16_t selected_color)
{
    const auto &button = kButtons[index];
    const bool is_color_selection = index < 3U;
    const bool selected =
        is_color_selection && button.color == selected_color;
    const bool pressed = static_cast<std::int8_t>(index) == pressed_button;
    const auto border = selected ? kYellow : kWhite;
    const auto fill = pressed ? 0x2104U : button.color;

    FillRect(pixels, button.x, button.y, button.width, button.height, fill);
    FillRect(pixels, button.x, button.y, button.width, 2U, border);
    FillRect(pixels, button.x, button.y + button.height - 2U,
             button.width, 2U, border);
    FillRect(pixels, button.x, button.y, 2U, button.height, border);
    FillRect(pixels, button.x + button.width - 2U, button.y, 2U,
             button.height, border);

    std::size_t length = 0U;
    while (button.label[length] != '\0') {
        ++length;
    }
    constexpr std::uint8_t kScale = 3U;
    const auto text_width = static_cast<std::uint16_t>(
        length * 7U * kScale - 2U * kScale);
    const auto text_x = static_cast<std::uint16_t>(
        button.x + ((button.width - text_width) / 2U));
    const auto text_y = static_cast<std::uint16_t>(
        button.y + ((button.height - 7U * kScale) / 2U));
    DrawText(pixels, text_x, text_y, button.label, kScale, kWhite);
}

void DrawMarker(std::uint16_t *pixels, std::uint16_t center_x,
                std::uint16_t center_y, std::uint16_t color)
{
    constexpr std::int16_t kRadius = 12;
    constexpr std::int16_t kCross = 20;
    for (std::int16_t offset = -kCross; offset <= kCross; ++offset) {
        PutPixel(pixels, static_cast<std::uint16_t>(center_x + offset),
                 center_y, color);
        PutPixel(pixels, center_x,
                 static_cast<std::uint16_t>(center_y + offset), color);
    }
    for (std::int16_t y = -kRadius; y <= kRadius; ++y) {
        for (std::int16_t x = -kRadius; x <= kRadius; ++x) {
            const auto distance = x * x + y * y;
            if (distance >= (kRadius - 1) * (kRadius - 1) &&
                distance <= (kRadius + 1) * (kRadius + 1)) {
                PutPixel(pixels,
                         static_cast<std::uint16_t>(center_x + x),
                         static_cast<std::uint16_t>(center_y + y), color);
            }
        }
    }
}

} // namespace

const char *TouchActionName(TouchAction action)
{
    switch (action) {
    case TouchAction::kSelectRed: return "RED";
    case TouchAction::kSelectGreen: return "GREEN";
    case TouchAction::kSelectBlue: return "BLUE";
    case TouchAction::kClearMarker: return "CLEAR";
    case TouchAction::kMarkImage: return "MARK";
    case TouchAction::kNone: break;
    }
    return "NONE";
}

DriverStatus OverlayUi::Initialize()
{
    if (initialized_) {
        return DriverStatus::kAlreadyInitialized;
    }
    initialized_ = true;
    return DriverStatus::kOk;
}

void OverlayUi::DrawOn(std::uint16_t *rgb565_frame) const
{
    if (!initialized_ || rgb565_frame == nullptr) {
        return;
    }

    DrawHeader(rgb565_frame);
    FillRect(rgb565_frame, 0U, 384U, kFrameWidth, 96U, kPanelColor);
    if (marker_visible_) {
        DrawMarker(rgb565_frame, marker_x_, marker_y_, selected_color_);
    }
    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        DrawButton(rgb565_frame, index, pressed_button_, selected_color_);
    }
}

TouchAction OverlayUi::PressAt(std::uint16_t x, std::uint16_t y)
{
    if (!initialized_) {
        return TouchAction::kNone;
    }

    for (std::size_t index = 0U; index < kButtonCount; ++index) {
        const auto &button = kButtons[index];
        const auto button_right = button.x + button.width;
        const auto button_bottom = button.y + button.height;
        if (x >= button.x && x < button_right && y >= button.y &&
            y < button_bottom) {
            pressed_button_ = static_cast<std::int8_t>(index);
            const auto action = button.action;
            switch (action) {
            case TouchAction::kSelectRed:
            case TouchAction::kSelectGreen:
            case TouchAction::kSelectBlue:
                selected_color_ = button.color;
                break;
            case TouchAction::kClearMarker:
                marker_visible_ = false;
                break;
            case TouchAction::kMarkImage:
            case TouchAction::kNone:
                break;
            }
            return action;
        }
    }

    if (y >= 90U && y < kButtonY - 20U) {
        marker_x_ = x;
        marker_y_ = y;
        marker_visible_ = true;
        return TouchAction::kMarkImage;
    }
    return TouchAction::kNone;
}

void OverlayUi::Release()
{
    pressed_button_ = -1;
}

} // namespace uai::camera_lcd_touch::driver
