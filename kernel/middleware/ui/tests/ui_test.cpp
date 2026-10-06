#include "middleware/ui/canvas.hpp"
#include "middleware/ui/widget.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

constexpr std::uint16_t kWidth = 32U;
constexpr std::uint16_t kHeight = 16U;
constexpr std::uint16_t kBackground = 0x0000U;
constexpr std::uint16_t kGuard = 0xA5A5U;

struct Frame {
    /* One extra row on each side detects writes outside the canvas. */
    std::array<std::uint16_t, kWidth * (kHeight + 2U)> pixels{};

    Frame()
    {
        pixels.fill(kGuard);
        for (std::size_t i = kWidth; i < kWidth * (kHeight + 1U); ++i) {
            pixels[i] = kBackground;
        }
    }
    std::uint16_t *Begin() { return pixels.data() + kWidth; }
    std::uint16_t At(std::uint16_t x, std::uint16_t y) const
    {
        return pixels[kWidth + static_cast<std::size_t>(y) * kWidth + x];
    }
    bool GuardsIntact() const
    {
        for (std::size_t i = 0U; i < kWidth; ++i) {
            if (pixels[i] != kGuard ||
                pixels[kWidth * (kHeight + 1U) + i] != kGuard) {
                return false;
            }
        }
        return true;
    }
};

TEST(UiCanvas, FillRectClipsToCanvasBounds)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    canvas.FillRect({28U, 12U, 10U, 10U}, 0xFFFFU);
    EXPECT_EQ(frame.At(28U, 12U), 0xFFFFU);
    EXPECT_EQ(frame.At(31U, 15U), 0xFFFFU);
    EXPECT_EQ(frame.At(27U, 12U), kBackground);
    EXPECT_EQ(frame.At(28U, 11U), kBackground);
    EXPECT_TRUE(frame.GuardsIntact());

    canvas.FillRect({40U, 0U, 4U, 4U}, 0xFFFFU);
    canvas.FillRect({0U, 20U, 4U, 4U}, 0xFFFFU);
    EXPECT_TRUE(frame.GuardsIntact());
}

TEST(UiCanvas, DrawFrameLeavesInteriorUntouched)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    canvas.DrawFrame({2U, 2U, 10U, 8U}, 2U, 0xF800U);
    EXPECT_EQ(frame.At(2U, 2U), 0xF800U);
    EXPECT_EQ(frame.At(3U, 3U), 0xF800U);
    EXPECT_EQ(frame.At(11U, 9U), 0xF800U);
    EXPECT_EQ(frame.At(4U, 4U), kBackground);
    EXPECT_EQ(frame.At(9U, 7U), kBackground);
    EXPECT_EQ(frame.At(1U, 1U), kBackground);
    EXPECT_EQ(frame.At(12U, 10U), kBackground);
}

TEST(UiCanvas, GlyphRendersAtScaleAndAdvance)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    /* 'I' is a top bar, a centred stem, and a bottom bar. */
    canvas.DrawText(1U, 1U, "I", 2U, 0x07E0U);
    for (std::uint16_t x = 1U; x < 11U; ++x) {
        EXPECT_EQ(frame.At(x, 1U), 0x07E0U) << "x=" << x;
        EXPECT_EQ(frame.At(x, 2U), 0x07E0U) << "x=" << x;
        EXPECT_EQ(frame.At(x, 14U), 0x07E0U) << "x=" << x;
    }
    EXPECT_EQ(frame.At(5U, 7U), 0x07E0U);
    EXPECT_EQ(frame.At(6U, 7U), 0x07E0U);
    EXPECT_EQ(frame.At(1U, 7U), kBackground);
    EXPECT_EQ(frame.At(10U, 7U), kBackground);
    EXPECT_EQ(frame.At(11U, 1U), kBackground);

    EXPECT_EQ(uai::ai::ui::TextWidth("AB", 3U), (2U * 6U - 1U) * 3U);
    EXPECT_EQ(uai::ai::ui::TextWidth("", 3U), 0U);
    EXPECT_EQ(uai::ai::ui::GlyphRow('a', 0U), uai::ai::ui::GlyphRow('A', 0U));
    EXPECT_EQ(uai::ai::ui::GlyphRow('#', 3U), 0U);
}

constexpr uai::ai::ui::ButtonSpec kButtons[] = {
    {1U, {4U, 4U, 10U, 6U}, "A", {}},
    {7U, {18U, 4U, 10U, 6U}, "B", {}},
};

TEST(UiButtonPanel, PressInsideThenReleaseEmitsTap)
{
    uai::ai::ui::ButtonPanel panel(kButtons, 2U);

    uai::ai::ui::Event event = panel.Update({true, 20U, 5U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kPress);
    EXPECT_EQ(event.widget_id, 7U);
    EXPECT_EQ(event.x, 20U);
    EXPECT_TRUE(panel.IsPressed(7U));
    EXPECT_FALSE(panel.IsPressed(1U));

    /* Holding produces no further events. */
    event = panel.Update({true, 21U, 5U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kNone);

    event = panel.Update({false, 0U, 0U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kTap);
    EXPECT_EQ(event.widget_id, 7U);
    EXPECT_FALSE(panel.IsPressed(7U));

    event = panel.Update({false, 0U, 0U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kNone);
}

TEST(UiButtonPanel, PressOutsideButtonsEmitsNothing)
{
    uai::ai::ui::ButtonPanel panel(kButtons, 2U);
    EXPECT_EQ(panel.Update({true, 15U, 5U}).type, uai::ai::ui::EventType::kNone);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
    /* Exclusive right/bottom edge. */
    EXPECT_EQ(panel.Update({true, 14U, 10U}).type, uai::ai::ui::EventType::kNone);
    panel.Update({false, 0U, 0U});
    EXPECT_EQ(panel.Update({true, 13U, 9U}).widget_id, 1U);
}

TEST(UiButtonPanel, PaintUsesPressedFillWhileHeld)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::ButtonSpec button{3U, {2U, 2U, 12U, 10U}, "", {}};
    button.style.fill = 0x1111U;
    button.style.pressed_fill = 0x2222U;
    button.style.border = 0x3333U;
    button.style.border_width = 1U;
    uai::ai::ui::ButtonPanel panel(&button, 1U);

    panel.Paint(canvas);
    EXPECT_EQ(frame.At(2U, 2U), 0x3333U);
    EXPECT_EQ(frame.At(7U, 7U), 0x1111U);
    EXPECT_EQ(frame.At(1U, 1U), kBackground);

    panel.Update({true, 7U, 7U});
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(7U, 7U), 0x2222U);
    EXPECT_EQ(frame.At(13U, 11U), 0x3333U);
    EXPECT_TRUE(frame.GuardsIntact());
}

TEST(UiButtonPanel, NullTableIsEmpty)
{
    uai::ai::ui::ButtonPanel panel(nullptr, 4U);
    EXPECT_EQ(panel.Count(), 0U);
    EXPECT_EQ(panel.Update({true, 1U, 1U}).type, uai::ai::ui::EventType::kNone);
}

} // namespace
