#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::ui {

struct Rect {
    std::uint16_t x = 0U;
    std::uint16_t y = 0U;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;

    constexpr bool Contains(
        std::uint16_t px,
        std::uint16_t py
    ) const
    {
        return px >= x && py >= y && static_cast<std::uint32_t>(px) < static_cast<std::uint32_t>(x) + width
            && static_cast<std::uint32_t>(py) < static_cast<std::uint32_t>(y) + height;
    }
};

/* True when pixel (px, py) lies inside the ellipse inscribed in `rect`.
 * Integer math on doubled coordinates; the host preview uses the same test. */
constexpr bool InsideEllipse(
    const Rect &rect,
    std::int32_t px,
    std::int32_t py
)
{
    if (rect.width == 0U || rect.height == 0U)
        return false;
    const std::int64_t a = rect.width; /* doubled semi-axes */
    const std::int64_t b = rect.height;
    const std::int64_t dx = 2 * (px - rect.x) + 1 - a;
    const std::int64_t dy = 2 * (py - rect.y) + 1 - b;
    return dx * dx * b * b + dy * dy * a * a <= a * a * b * b;
}

/* Outline of a button-like widget, inscribed in its bounds. Hit testing and
 * painting use the same InsideShape() predicate. */
enum class Shape : std::uint8_t {
    kRectangle,
    kRounded, /* corner radius = min(width, height) / 4 */
    kPill,    /* corner radius = min(width, height) / 2 */
    kEllipse,
    kTriangleUp, /* apex at the top edge centre */
    kTriangleDown,
    kTriangleLeft,
    kTriangleRight,
    kDiamond,
};

constexpr std::uint16_t CornerRadiusOf(
    Shape shape,
    const Rect &rect
)
{
    const std::uint16_t side = rect.width < rect.height ? rect.width : rect.height;
    if (shape == Shape::kRounded)
        return side / 4U;
    if (shape == Shape::kPill)
        return side / 2U;
    return 0U;
}

constexpr bool InsideShape(
    Shape shape,
    const Rect &rect,
    std::int32_t px,
    std::int32_t py
)
{
    if (px < rect.x || py < rect.y || px >= rect.x + rect.width || py >= rect.y + rect.height) {
        return false;
    }
    if (shape == Shape::kRectangle)
        return true;
    if (shape == Shape::kEllipse)
        return InsideEllipse(rect, px, py);
    const std::int64_t w = rect.width; /* doubled extents */
    const std::int64_t h = rect.height;
    const std::int64_t dx = 2 * (px - rect.x) + 1 - w;
    const std::int64_t dy = 2 * (py - rect.y) + 1 - h;
    const std::int64_t ax = dx < 0 ? -dx : dx;
    const std::int64_t ay = dy < 0 ? -dy : dy;
    switch (shape) {
    case Shape::kRounded:
    case Shape::kPill: {
        const std::int64_t r2 = 2 * CornerRadiusOf(shape, rect); /* doubled radius */
        const std::int64_t cx = w - r2, cy = h - r2;             /* corner centres */
        if (ax <= cx || ay <= cy)
            return true;
        return (ax - cx) * (ax - cx) + (ay - cy) * (ay - cy) <= r2 * r2;
    }
    case Shape::kDiamond:
        return ax * h + ay * w <= w * h;
    /* The apex row keeps the two centre pixels: edges are offset by half a
     * pixel (+1 in doubled units) so pixel-centre sampling never loses it. */
    case Shape::kTriangleUp:
        return ax * 2 * h <= w * (dy + h + 1);
    case Shape::kTriangleDown:
        return ax * 2 * h <= w * (h - dy + 1);
    case Shape::kTriangleLeft:
        return ay * 2 * w <= h * (dx + w + 1);
    case Shape::kTriangleRight:
        return ay * 2 * w <= h * (w - dx + 1);
    default:
        return true;
    }
}

constexpr std::uint16_t Rgb565(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue
)
{
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) | ((green & 0xFCU) << 3U) | (blue >> 3U));
}

/* Built-in 5x7 glyphs; the host renderer in host_app/ui_designer mirrors
 * this table so the preview and the device output are pixel identical. */
inline constexpr std::uint8_t kGlyphWidth = 5U;
inline constexpr std::uint8_t kGlyphHeight = 7U;
inline constexpr std::uint8_t kGlyphAdvance = 6U;

std::uint8_t GlyphRow(
    char letter,
    std::uint8_t row
);

constexpr std::uint32_t TextWidth(
    const char *text,
    std::uint8_t scale
)
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
    Canvas(
        std::uint16_t *pixels,
        std::uint16_t width,
        std::uint16_t height
    )
        : pixels_(pixels),
          width_(width),
          height_(height)
    {}

    std::uint16_t Width() const { return width_; }
    std::uint16_t Height() const { return height_; }

    void PutPixel(
        std::uint16_t x,
        std::uint16_t y,
        std::uint16_t color
    );
    void FillRect(
        const Rect &rect,
        std::uint16_t color
    );
    void DrawFrame(
        const Rect &rect,
        std::uint16_t thickness,
        std::uint16_t color
    );
    void FillEllipse(
        const Rect &rect,
        std::uint16_t color
    );
    /* Ring between the ellipse of `rect` and the one inset by `thickness`. */
    void DrawEllipseFrame(
        const Rect &rect,
        std::uint16_t thickness,
        std::uint16_t color
    );
    void FillShape(
        Shape shape,
        const Rect &rect,
        std::uint16_t color
    );
    /* Band between the shape of `rect` and the same shape inset by
     * `thickness` on every side. */
    void DrawShapeFrame(
        Shape shape,
        const Rect &rect,
        std::uint16_t thickness,
        std::uint16_t color
    );
    /* Copies an RGB565 bitmap; pixels equal to `transparent` are skipped
     * when `has_transparent` is set. */
    void Blit(
        std::uint16_t x,
        std::uint16_t y,
        const std::uint16_t *pixels,
        std::uint16_t width,
        std::uint16_t height,
        bool has_transparent = false,
        std::uint16_t transparent = 0U
    );
    void DrawText(
        std::uint16_t x,
        std::uint16_t y,
        const char *text,
        std::uint8_t scale,
        std::uint16_t color
    );
    void DrawTextCentered(
        const Rect &rect,
        const char *text,
        std::uint8_t scale,
        std::uint16_t color
    );

private:
    std::uint16_t *pixels_;
    std::uint16_t width_;
    std::uint16_t height_;
};

} // namespace uai::ai::ui
