#include "middleware/ui/canvas.hpp"
#include "middleware/ui/widget.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kWidth = 32U;
constexpr std::uint16_t kHeight = 16U;
constexpr std::uint16_t kBackground = 0x0000U;
constexpr std::uint16_t kGuard = 0xA5A5U;

template <std::uint16_t Height>
struct FrameOf {
    /* One extra row on each side detects writes outside the canvas. */
    std::array<std::uint16_t, kWidth * (Height + 2U)> pixels{};

    FrameOf()
    {
        pixels.fill(kGuard);
        for (std::size_t i = kWidth; i < kWidth * (Height + 1U); ++i) {
            pixels[i] = kBackground;
        }
    }
    std::uint16_t *Begin() { return pixels.data() + kWidth; }
    std::uint16_t At(std::uint16_t x, std::uint16_t y) const
    {
        return pixels.at(kWidth + static_cast<std::size_t>(y) * kWidth + x);
    }
    bool GuardsIntact() const
    {
        for (std::size_t i = 0U; i < kWidth; ++i) {
            if (pixels[i] != kGuard ||
                pixels[kWidth * (Height + 1U) + i] != kGuard) {
                return false;
            }
        }
        return true;
    }
};
using Frame = FrameOf<kHeight>;
using TallFrame = FrameOf<32U>;

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

TEST(UiButtonPanel, CheckedFillUntilPressedOverrides)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::ButtonSpec button{3U, {2U, 2U, 12U, 10U}, "", {}};
    button.style.fill = 0x1111U;
    button.style.pressed_fill = 0x2222U;
    button.style.checked_fill = 0x3333U;
    button.style.border_width = 0U;
    uai::ai::ui::ButtonPanel panel(&button, 1U);

    EXPECT_FALSE(panel.IsChecked(3U));
    panel.SetChecked(3U, true);
    panel.SetChecked(99U, true);
    EXPECT_TRUE(panel.IsChecked(3U));
    EXPECT_FALSE(panel.IsChecked(99U));
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(7U, 7U), 0x3333U);

    panel.Update({true, 7U, 7U});
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(7U, 7U), 0x2222U);
    panel.Update({false, 0U, 0U});
    panel.SetChecked(3U, false);
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(7U, 7U), 0x1111U);
}

TEST(UiLabelPanel, InitialTextAlignmentAndRuntimeReplacement)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    /* 'I' at scale 1 is 5 px wide; right-aligned in a 20 px box it ends at
     * x = 2 + 20 - 1 = 21. */
    uai::ai::ui::LabelSpec label{5U, {2U, 2U, 20U, 9U}, "I", {}};
    label.style.text = 0x07E0U;
    label.style.fill = 0x1111U;
    label.style.text_scale = 1U;
    label.style.align = uai::ai::ui::TextAlign::kRight;
    label.style.padding = 0U;
    uai::ai::ui::LabelPanel panel(&label, 1U);

    EXPECT_STREQ(panel.Text(5U), "I");
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(2U, 2U), 0x1111U);
    EXPECT_EQ(frame.At(17U, 3U), 0x07E0U);
    EXPECT_EQ(frame.At(21U, 3U), 0x07E0U);
    EXPECT_EQ(frame.At(16U, 3U), 0x1111U);

    EXPECT_TRUE(panel.SetText(5U, "II"));
    EXPECT_FALSE(panel.SetText(6U, "X"));
    EXPECT_FALSE(panel.SetText(5U, nullptr));
    EXPECT_STREQ(panel.Text(5U), "II");
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(11U, 3U), 0x07E0U);

    label.style.has_fill = false;
    label.style.align = uai::ai::ui::TextAlign::kLeft;
    Frame clean;
    uai::ai::ui::Canvas clean_canvas(clean.Begin(), kWidth, kHeight);
    uai::ai::ui::LabelPanel transparent(&label, 1U);
    transparent.Paint(clean_canvas);
    EXPECT_EQ(clean.At(2U, 3U), 0x07E0U);
    EXPECT_EQ(clean.At(8U, 8U), kBackground);
    EXPECT_TRUE(clean.GuardsIntact());
}

TEST(UiLabelPanel, TextIsTruncatedToCapacity)
{
    uai::ai::ui::LabelSpec label{1U, {0U, 0U, 8U, 8U}, "", {}};
    uai::ai::ui::LabelPanel panel(&label, 1U);
    std::string long_text(uai::ai::ui::kLabelTextCapacity + 10U, 'A');
    EXPECT_TRUE(panel.SetText(1U, long_text.c_str()));
    EXPECT_EQ(std::string(panel.Text(1U)).size(),
              uai::ai::ui::kLabelTextCapacity - 1U);
}

TEST(UiLabelPanel, PaddingInsetsAlignedText)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::LabelSpec label{1U, {0U, 0U, 32U, 9U}, "I", {}};
    label.style.text = 0x07E0U;
    label.style.has_fill = false;
    label.style.text_scale = 1U;
    label.style.padding = 4U;
    uai::ai::ui::LabelPanel left(&label, 1U);
    left.Paint(canvas);
    EXPECT_EQ(frame.At(4U, 1U), 0x07E0U);
    EXPECT_EQ(frame.At(3U, 1U), kBackground);

    label.style.align = uai::ai::ui::TextAlign::kRight;
    uai::ai::ui::LabelPanel right(&label, 1U);
    right.Paint(canvas);
    EXPECT_EQ(frame.At(27U, 1U), 0x07E0U);
    EXPECT_EQ(frame.At(28U, 1U), kBackground);
}

TEST(UiPainterGroup, PaintsInOrder)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::LabelSpec under{1U, {0U, 0U, 8U, 8U}, "", {}};
    under.style.fill = 0x1111U;
    uai::ai::ui::LabelSpec over{2U, {4U, 4U, 8U, 8U}, "", {}};
    over.style.fill = 0x2222U;
    uai::ai::ui::LabelPanel first(&under, 1U);
    uai::ai::ui::LabelPanel second(&over, 1U);
    const uai::ai::ui::Painter *painters[] = {&first, nullptr, &second};
    uai::ai::ui::PainterGroup group(painters, 3U);
    group.Paint(canvas);
    EXPECT_EQ(frame.At(1U, 1U), 0x1111U);
    EXPECT_EQ(frame.At(5U, 5U), 0x2222U);
    EXPECT_EQ(frame.At(11U, 11U), 0x2222U);
}

TEST(UiButtonPanel, IconReplacesLabel)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::ButtonSpec button{1U, {0U, 0U, 16U, 16U}, "X", {}, uai::ai::ui::Icon::kMenu};
    button.style.fill = 0x0001U;
    button.style.text = 0xFFFFU;
    button.style.border_width = 0U;
    uai::ai::ui::ButtonPanel panel(&button, 1U);
    panel.Paint(canvas);
    /* extent 8, bar 1: bars at y = 4, 7.5->7, 11 spanning x = 4..11 */
    EXPECT_EQ(frame.At(4U, 4U), 0xFFFFU);
    EXPECT_EQ(frame.At(11U, 4U), 0xFFFFU);
    EXPECT_EQ(frame.At(4U, 11U), 0xFFFFU);
    EXPECT_EQ(frame.At(4U, 5U), 0x0001U);
    EXPECT_EQ(frame.At(3U, 4U), 0x0001U);
}

/* 32 px wide: track x = 8..23 (width 16), so 15 px of travel across the
 * value span. A 1x caption keeps the track inside the 16-row frame. */
constexpr uai::ai::ui::SliderStyle SmallSliderStyle()
{
    uai::ai::ui::SliderStyle style{};
    style.text_scale = 1U;
    style.show_value = false;
    return style;
}
constexpr uai::ai::ui::SliderSpec kSlider{
    9U, {0U, 0U, 32U, 16U}, "", 0, 30, 10, 10, SmallSliderStyle()};

TEST(UiSlider, ValueFollowsFingerAndSnapsToStep)
{
    uai::ai::ui::SliderPanel panel(&kSlider, 1U);
    EXPECT_EQ(panel.Value(9U), 10);
    const uai::ai::ui::Rect track = uai::ai::ui::SliderPanel::TrackOf(kSlider);
    EXPECT_EQ(track.x, 8U);
    EXPECT_EQ(track.width, 16U);

    /* Press at the far right: value jumps to max and reports kChange. */
    uai::ai::ui::Event event = panel.Update({true, 23U, 8U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(event.widget_id, 9U);
    EXPECT_EQ(event.value, 30);
    EXPECT_TRUE(panel.IsDragging());

    /* Dragging left of the track clamps to min; the same value is silent. */
    event = panel.Update({true, 0U, 8U});
    EXPECT_EQ(event.value, 0);
    EXPECT_EQ(panel.Update({true, 2U, 8U}).type, uai::ai::ui::EventType::kNone);

    /* Mid-track snaps to the 10 step grid. */
    event = panel.Update({true, 15U, 8U});
    EXPECT_EQ(event.value, 10);
    EXPECT_EQ(panel.Update({true, 19U, 8U}).value, 20);

    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
    EXPECT_FALSE(panel.IsDragging());
    EXPECT_EQ(panel.Value(9U), 20);

    /* A touch that starts outside never captures the slider. */
    EXPECT_EQ(panel.Update({true, 10U, 20U}).type, uai::ai::ui::EventType::kNone);
    EXPECT_EQ(panel.Update({true, 23U, 8U}).type, uai::ai::ui::EventType::kNone);
    panel.Update({false, 0U, 0U});

    EXPECT_TRUE(panel.SetValue(9U, 99));
    EXPECT_EQ(panel.Value(9U), 30);
    EXPECT_FALSE(panel.SetValue(1U, 0));
}

TEST(UiSlider, PaintShowsFillUpToKnob)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::SliderSpec slider = kSlider;
    slider.style.track = 0x1111U;
    slider.style.fill = 0x2222U;
    slider.style.knob = 0x3333U;
    slider.initial = 30;
    uai::ai::ui::SliderPanel panel(&slider, 1U);
    panel.Paint(canvas);
    const uai::ai::ui::Rect track = uai::ai::ui::SliderPanel::TrackOf(slider);
    /* Fully filled: left of the knob is fill, the knob covers the right end. */
    EXPECT_EQ(frame.At(track.x, track.y), 0x2222U);
    EXPECT_EQ(frame.At(static_cast<std::uint16_t>(track.x + track.width - 1U), track.y), 0x3333U);

    panel.SetValue(9U, 0);
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(static_cast<std::uint16_t>(track.x + track.width - 1U), track.y), 0x1111U);
    EXPECT_EQ(frame.At(track.x, track.y), 0x3333U);
}

TEST(UiScreen, SolidBackgroundAndRouting)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    static constexpr uai::ai::ui::ButtonSpec buttons[] = {
        {1U, {0U, 0U, 8U, 8U}, "", {}},
    };
    static constexpr uai::ai::ui::SliderSpec sliders[] = {kSlider};
    uai::ai::ui::ScreenSpec spec{};
    spec.id = 1U;
    spec.background = uai::ai::ui::Background::kSolid;
    spec.color = 0x4444U;
    spec.buttons = buttons;
    spec.button_count = 1U;
    spec.sliders = sliders;
    spec.slider_count = 1U;
    uai::ai::ui::Screen screen(spec);

    screen.Paint(canvas);
    EXPECT_EQ(frame.At(31U, 15U), 0x4444U);
    EXPECT_TRUE(frame.GuardsIntact());

    EXPECT_EQ(screen.Update({true, 2U, 2U}).type, uai::ai::ui::EventType::kPress);
    EXPECT_EQ(screen.Update({false, 0U, 0U}).widget_id, 1U);
    const uai::ai::ui::Event change = screen.Update({true, 23U, 10U});
    EXPECT_EQ(change.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(change.widget_id, 9U);
    EXPECT_EQ(screen.Sliders().Value(9U), 30);

    uai::ai::ui::ScreenSpec camera{};
    uai::ai::ui::Screen empty(camera);
    Frame untouched;
    uai::ai::ui::Canvas untouched_canvas(untouched.Begin(), kWidth, kHeight);
    empty.Paint(untouched_canvas);
    EXPECT_EQ(untouched.At(0U, 0U), kBackground);
}

TEST(UiCanvas, EllipseFillAndFrameStayInsideBounds)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    const uai::ai::ui::Rect rect{2U, 2U, 16U, 12U};
    canvas.FillEllipse(rect, 0x1111U);
    EXPECT_EQ(frame.At(10U, 8U), 0x1111U);   /* centre */
    EXPECT_EQ(frame.At(2U, 8U), 0x1111U);    /* left extreme on the centre row */
    EXPECT_EQ(frame.At(2U, 2U), kBackground); /* corner stays empty */
    EXPECT_EQ(frame.At(17U, 13U), kBackground);
    EXPECT_FALSE(uai::ai::ui::InsideEllipse(rect, 2, 2));
    EXPECT_TRUE(uai::ai::ui::InsideEllipse(rect, 10, 8));

    canvas.DrawEllipseFrame(rect, 2U, 0x2222U);
    EXPECT_EQ(frame.At(2U, 8U), 0x2222U);
    EXPECT_EQ(frame.At(10U, 8U), 0x1111U);  /* interior keeps the fill */
    EXPECT_TRUE(frame.GuardsIntact());
}

TEST(UiButtonPanel, EllipseShapeHitTestFollowsOutline)
{
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::ButtonSpec button{1U, {0U, 0U, 16U, 16U}, "", {}, uai::ai::ui::Icon::kNone,
                                   uai::ai::ui::Shape::kEllipse};
    button.style.fill = 0x1111U;
    button.style.border_width = 0U;
    uai::ai::ui::ButtonPanel panel(&button, 1U);
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(8U, 8U), 0x1111U);
    EXPECT_EQ(frame.At(0U, 0U), kBackground);
    /* The corner is inside the bounds but outside the circle. */
    EXPECT_EQ(panel.Update({true, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
    panel.Update({false, 0U, 0U});
    EXPECT_EQ(panel.Update({true, 8U, 8U}).type, uai::ai::ui::EventType::kPress);
}

/* 32x32 bounds, 1x caption (11 px): disc 21x21 at (5, 11). */
constexpr uai::ai::ui::DialStyle SmallDialStyle()
{
    uai::ai::ui::DialStyle style{};
    style.text_scale = 1U;
    style.show_value = false;
    return style;
}
constexpr uai::ai::ui::DialSpec kDial{
    4U, {0U, 0U, 32U, 32U}, "", 0, 100, 10, 50, SmallDialStyle()};

TEST(UiDial, AngleMapsToValueAcrossTheArc)
{
    const uai::ai::ui::Rect disc = uai::ai::ui::DialPanel::DiscOf(kDial);
    EXPECT_EQ(disc.width, 21U);
    EXPECT_EQ(disc.x, 5U);
    EXPECT_EQ(disc.y, 11U);
    const std::uint16_t cx = static_cast<std::uint16_t>(disc.x + disc.width / 2U);
    const std::uint16_t cy = static_cast<std::uint16_t>(disc.y + disc.height / 2U);
    /* Straight up is the middle of the 270-degree sweep. */
    EXPECT_EQ(uai::ai::ui::DialPanel::ValueAt(kDial, cx, static_cast<std::uint16_t>(cy - 8U)), 50);
    /* Left (180 degrees) is 45 of 270 into the sweep: 16.7 -> snaps to 20. */
    EXPECT_EQ(uai::ai::ui::DialPanel::ValueAt(kDial, static_cast<std::uint16_t>(cx - 8U), cy), 20);
    EXPECT_EQ(uai::ai::ui::DialPanel::ValueAt(kDial, static_cast<std::uint16_t>(cx + 8U), cy), 80);
    /* Bottom-left corner of the gap snaps to min, bottom-right to max. */
    EXPECT_EQ(uai::ai::ui::DialPanel::ValueAt(kDial, static_cast<std::uint16_t>(cx - 2U),
                                              static_cast<std::uint16_t>(cy + 9U)), 0);
    EXPECT_EQ(uai::ai::ui::DialPanel::ValueAt(kDial, static_cast<std::uint16_t>(cx + 2U),
                                              static_cast<std::uint16_t>(cy + 9U)), 100);
    EXPECT_EQ(uai::ai::ui::DialPanel::SweepOf(kDial, 50), 135U);
    EXPECT_EQ(uai::ai::ui::DialPanel::SweepOf(kDial, 100), 270U);
}

TEST(UiDial, DragInsideDiscChangesValueAndPaintsArc)
{
    uai::ai::ui::DialPanel panel(&kDial, 1U);
    EXPECT_EQ(panel.Value(4U), 50);
    const uai::ai::ui::Rect disc = uai::ai::ui::DialPanel::DiscOf(kDial);
    const std::uint16_t cx = static_cast<std::uint16_t>(disc.x + disc.width / 2U);
    const std::uint16_t cy = static_cast<std::uint16_t>(disc.y + disc.height / 2U);

    /* Touching the caption row (outside the disc) does nothing. */
    EXPECT_EQ(panel.Update({true, cx, 2U}).type, uai::ai::ui::EventType::kNone);
    panel.Update({false, 0U, 0U});

    uai::ai::ui::Event event = panel.Update({true, static_cast<std::uint16_t>(cx + 8U), cy});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(event.value, 80);
    EXPECT_TRUE(panel.IsDragging());
    EXPECT_EQ(panel.Update({true, static_cast<std::uint16_t>(cx + 8U), cy}).type,
              uai::ai::ui::EventType::kNone);
    panel.Update({false, 0U, 0U});
    EXPECT_EQ(panel.Value(4U), 80);
    EXPECT_TRUE(panel.SetValue(4U, 500));
    EXPECT_EQ(panel.Value(4U), 100);

    TallFrame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, 32U);
    uai::ai::ui::DialSpec dial = kDial;
    dial.style.face = 0x1111U;
    dial.style.track = 0x2222U;
    dial.style.fill = 0x3333U;
    dial.style.pointer = 0x4444U;
    dial.initial = 50;
    uai::ai::ui::DialPanel painted(&dial, 1U);
    painted.Paint(canvas);
    EXPECT_EQ(frame.At(cx, cy), 0x1111U);                                   /* face */
    EXPECT_EQ(frame.At(static_cast<std::uint16_t>(cx - 9U), cy), 0x3333U); /* left ring: swept */
    EXPECT_EQ(frame.At(static_cast<std::uint16_t>(cx + 9U), cy), 0x2222U); /* right ring: not yet */
    EXPECT_EQ(frame.At(cx, static_cast<std::uint16_t>(cy - 9U)), 0x4444U); /* pointer at top */
    EXPECT_EQ(frame.At(cx, static_cast<std::uint16_t>(cy + 9U)), kBackground); /* gap */
}

constexpr const char *const kWheelItems[] = {"A", "B", "C", "D"};
constexpr uai::ai::ui::WheelStyle SmallWheelStyle()
{
    uai::ai::ui::WheelStyle style{};
    style.text_scale = 1U;
    return style;
}
/* Row height 15; bounds show one band plus partial neighbours. */
constexpr uai::ai::ui::WheelSpec kWheel{
    6U, {0U, 0U, 32U, 16U}, kWheelItems, 4U, 1U, SmallWheelStyle()};

TEST(UiWheel, DragStepsThroughItems)
{
    uai::ai::ui::WheelPanel panel(&kWheel, 1U);
    EXPECT_EQ(panel.Value(6U), 1);
    EXPECT_STREQ(panel.ItemText(6U), "B");
    EXPECT_EQ(uai::ai::ui::WheelPanel::RowHeightOf(kWheel), 15U);

    /* Dragging up by one row selects the next item; two rows the one after. */
    EXPECT_EQ(panel.Update({true, 16U, 15U}).type, uai::ai::ui::EventType::kNone);
    EXPECT_EQ(panel.Update({true, 16U, 10U}).type, uai::ai::ui::EventType::kNone);
    uai::ai::ui::Event event = panel.Update({true, 16U, 0U});
    EXPECT_EQ(event.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(event.value, 2);
    EXPECT_STREQ(panel.ItemText(6U), "C");
    panel.Update({false, 0U, 0U});

    /* Dragging down past the first item clamps at 0. */
    panel.Update({true, 16U, 0U});
    event = panel.Update({true, 16U, 15U});
    EXPECT_EQ(event.value, 1);
    EXPECT_EQ(panel.Update({true, 16U, 15U}).type, uai::ai::ui::EventType::kNone);
    panel.Update({false, 0U, 0U});
    panel.Update({true, 16U, 0U});
    panel.Update({true, 16U, 15U});
    EXPECT_EQ(panel.Value(6U), 0);
    panel.Update({false, 0U, 0U});

    EXPECT_TRUE(panel.SetValue(6U, 3));
    EXPECT_FALSE(panel.SetValue(6U, 4));
    EXPECT_STREQ(panel.ItemText(6U), "D");

    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    uai::ai::ui::WheelSpec wheel = kWheel;
    wheel.style.fill = 0x1111U;
    wheel.style.highlight = 0x2222U;
    wheel.style.border = 0x3333U;
    uai::ai::ui::WheelPanel painted(&wheel, 1U);
    painted.Paint(canvas);
    EXPECT_EQ(frame.At(0U, 0U), 0x3333U);  /* border */
    EXPECT_EQ(frame.At(2U, 8U), 0x2222U);  /* band covers the middle row */
    EXPECT_TRUE(frame.GuardsIntact());
}

TEST(UiNumber, FormatsDecimalsAndUnit)
{
    uai::ai::ui::NumberSpec number{7U, {0U, 0U, 32U, 16U}, "", "%", 1U, 0, {}};
    char text[uai::ai::ui::kNumberTextCapacity];
    uai::ai::ui::NumberPanel::Format(number, 1234, text);
    EXPECT_STREQ(text, "123.4%");
    uai::ai::ui::NumberPanel::Format(number, -5, text);
    EXPECT_STREQ(text, "-0.5%");
    number.decimals = 0U;
    number.unit = "";
    uai::ai::ui::NumberPanel::Format(number, 42, text);
    EXPECT_STREQ(text, "42");

    uai::ai::ui::NumberPanel panel(&number, 1U);
    EXPECT_EQ(panel.Value(7U), 0);
    EXPECT_TRUE(panel.SetValue(7U, 9));
    EXPECT_FALSE(panel.SetValue(8U, 9));
    EXPECT_EQ(panel.Value(7U), 9);

    /* Right-aligned 1x "9" ends 4 px before the right edge. */
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    number.style.text_scale = 1U;
    number.style.fill = 0x1111U;
    number.style.text = 0x2222U;
    uai::ai::ui::NumberPanel painted(&number, 1U);
    painted.SetValue(7U, 9);
    painted.Paint(canvas);
    EXPECT_EQ(frame.At(1U, 1U), 0x1111U);
    EXPECT_EQ(frame.At(25U, 4U), 0x2222U);  /* top bar of the '9' */
    EXPECT_EQ(frame.At(27U, 5U), 0x2222U);  /* right stem */
    EXPECT_EQ(frame.At(28U, 5U), 0x1111U);
}

TEST(UiImage, BlitSkipsTransparentKey)
{
    static constexpr std::uint16_t kBitmap[] = {
        0xAAAAU, 0xF81FU,
        0xF81FU, 0xBBBBU,
    };
    uai::ai::ui::ImageSpec image{9U, {30U, 14U, 2U, 2U}, kBitmap, true, 0xF81FU};
    uai::ai::ui::ImagePanel panel(&image, 1U);
    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(30U, 14U), 0xAAAAU);
    EXPECT_EQ(frame.At(31U, 14U), kBackground);
    EXPECT_EQ(frame.At(30U, 15U), kBackground);
    EXPECT_EQ(frame.At(31U, 15U), 0xBBBBU);

    /* Partially off-canvas images are clipped, never wrapped. */
    uai::ai::ui::ImageSpec edge{10U, {31U, 15U, 2U, 2U}, kBitmap, false, 0U};
    uai::ai::ui::ImagePanel clipped(&edge, 1U);
    clipped.Paint(canvas);
    EXPECT_EQ(frame.At(31U, 15U), 0xAAAAU);
    EXPECT_EQ(frame.At(0U, 0U), kBackground);
    EXPECT_TRUE(frame.GuardsIntact());
}

/* Shape predicates on a 16x16 square: corners, apexes and centres. */
TEST(UiCanvas, InsideShapeFollowsEachOutline)
{
    using uai::ai::ui::InsideShape;
    using uai::ai::ui::Shape;
    constexpr uai::ai::ui::Rect kBox{0U, 0U, 16U, 16U};

    static_assert(InsideShape(Shape::kRectangle, kBox, 0, 0));
    static_assert(!InsideShape(Shape::kRectangle, kBox, 16, 8));

    /* Rounded (radius 4): the very corner is cut, one radius in is kept. */
    static_assert(!InsideShape(Shape::kRounded, kBox, 0, 0));
    static_assert(InsideShape(Shape::kRounded, kBox, 1, 1));
    static_assert(InsideShape(Shape::kRounded, kBox, 0, 4));
    /* Pill (radius 8) on a square is the ellipse. */
    static_assert(!InsideShape(Shape::kPill, kBox, 1, 1));
    static_assert(InsideShape(Shape::kPill, kBox, 8, 0));

    /* Triangle up: apex row keeps only the centre columns. */
    static_assert(InsideShape(Shape::kTriangleUp, kBox, 7, 0));
    static_assert(InsideShape(Shape::kTriangleUp, kBox, 8, 0));
    static_assert(!InsideShape(Shape::kTriangleUp, kBox, 5, 0));
    static_assert(InsideShape(Shape::kTriangleUp, kBox, 0, 15));
    static_assert(InsideShape(Shape::kTriangleUp, kBox, 15, 15));
    static_assert(!InsideShape(Shape::kTriangleDown, kBox, 0, 15));
    static_assert(InsideShape(Shape::kTriangleDown, kBox, 0, 0));
    static_assert(InsideShape(Shape::kTriangleLeft, kBox, 0, 8));
    static_assert(!InsideShape(Shape::kTriangleLeft, kBox, 0, 0));
    static_assert(InsideShape(Shape::kTriangleLeft, kBox, 15, 0));
    static_assert(InsideShape(Shape::kTriangleRight, kBox, 15, 8));
    static_assert(!InsideShape(Shape::kTriangleRight, kBox, 15, 0));

    /* Diamond: edge midpoints in, corners out. */
    static_assert(InsideShape(Shape::kDiamond, kBox, 8, 0));
    static_assert(InsideShape(Shape::kDiamond, kBox, 0, 8));
    static_assert(!InsideShape(Shape::kDiamond, kBox, 0, 0));
    static_assert(!InsideShape(Shape::kDiamond, kBox, 15, 15));

    Frame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, kHeight);
    canvas.FillShape(Shape::kTriangleRight, {0U, 0U, 16U, 16U}, 0x1111U);
    canvas.DrawShapeFrame(Shape::kDiamond, {16U, 0U, 16U, 16U}, 2U, 0x2222U);
    EXPECT_EQ(frame.At(15U, 8U), 0x1111U);
    EXPECT_EQ(frame.At(15U, 0U), kBackground);
    EXPECT_EQ(frame.At(0U, 0U), 0x1111U);
    EXPECT_EQ(frame.At(24U, 0U), 0x2222U);     /* diamond top apex */
    EXPECT_EQ(frame.At(24U, 8U), kBackground); /* interior left open */
    EXPECT_EQ(frame.At(16U, 0U), kBackground);
    EXPECT_TRUE(frame.GuardsIntact());
}

TEST(UiButtonPanel, TriangleButtonHitTestFollowsOutline)
{
    uai::ai::ui::ButtonSpec button{};
    button.id = 3U;
    button.bounds = {0U, 0U, 16U, 16U};
    button.shape = uai::ai::ui::Shape::kTriangleLeft;
    uai::ai::ui::ButtonPanel panel(&button, 1U);

    EXPECT_EQ(panel.Update({true, 15U, 0U}).type, uai::ai::ui::EventType::kPress);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kTap);
    /* Top-left corner lies outside the left-pointing triangle. */
    EXPECT_EQ(panel.Update({true, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
}

TEST(UiPad, SegmentsTapAndRingRotationDetents)
{
    using uai::ai::ui::EventType;
    using uai::ai::ui::PadSegment;
    uai::ai::ui::PadSpec pad{};
    pad.id = 7U;
    pad.bounds = {0U, 0U, 32U, 32U};
    uai::ai::ui::PadPanel panel(&pad, 1U);

    const uai::ai::ui::Rect center = uai::ai::ui::PadPanel::CenterOf(pad);
    EXPECT_EQ(center.width, 12U);
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 16U, 2U), static_cast<std::int32_t>(PadSegment::kUp));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 29U, 16U), static_cast<std::int32_t>(PadSegment::kRight));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 16U, 29U), static_cast<std::int32_t>(PadSegment::kDown));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 2U, 16U), static_cast<std::int32_t>(PadSegment::kLeft));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 16U, 16U), static_cast<std::int32_t>(PadSegment::kCenter));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 0U, 0U), -1);
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 22U, 9U), static_cast<std::int32_t>(PadSegment::kRight));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 22U, 22U), static_cast<std::int32_t>(PadSegment::kDown));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 9U, 22U), static_cast<std::int32_t>(PadSegment::kLeft));
    EXPECT_EQ(uai::ai::ui::PadPanel::SegmentAt(pad, 9U, 9U), static_cast<std::int32_t>(PadSegment::kUp));

    /* Press and release on the same quadrant: kPress then kTap carrying it. */
    uai::ai::ui::Event event = panel.Update({true, 16U, 2U});
    EXPECT_EQ(event.type, EventType::kPress);
    EXPECT_EQ(event.value, static_cast<std::int32_t>(PadSegment::kUp));
    EXPECT_EQ(panel.Pressed(7U), static_cast<std::int32_t>(PadSegment::kUp));
    event = panel.Update({false, 0U, 0U});
    EXPECT_EQ(event.type, EventType::kTap);
    EXPECT_EQ(event.value, static_cast<std::int32_t>(PadSegment::kUp));
    EXPECT_EQ(panel.Pressed(7U), -1);

    /* Sliding clockwise from the top (0 deg) to the right (90 deg) passes
     * two 45-degree detents; the release is then not a tap. */
    EXPECT_EQ(panel.Update({true, 16U, 2U}).type, EventType::kPress);
    event = panel.Update({true, 26U, 6U});   /* ~45 deg */
    EXPECT_EQ(event.type, EventType::kChange);
    EXPECT_EQ(event.value, 1);
    event = panel.Update({true, 29U, 16U});  /* 90 deg */
    EXPECT_EQ(event.type, EventType::kChange);
    EXPECT_EQ(event.value, 1);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, EventType::kNone);

    /* Counter-clockwise gives negative steps; a centre press never rotates, and
     * sliding off the centre cancels the tap. */
    EXPECT_EQ(panel.Update({true, 16U, 2U}).type, EventType::kPress);
    EXPECT_EQ(panel.Update({true, 2U, 16U}).value, -2);
    panel.Update({false, 0U, 0U});
    EXPECT_EQ(panel.Update({true, 16U, 16U}).value, static_cast<std::int32_t>(PadSegment::kCenter));
    EXPECT_EQ(panel.Update({true, 29U, 16U}).type, EventType::kNone);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, EventType::kNone);
    EXPECT_EQ(panel.Update({true, 16U, 16U}).type, EventType::kPress);
    event = panel.Update({false, 0U, 0U});
    EXPECT_EQ(event.type, EventType::kTap);
    EXPECT_EQ(event.value, static_cast<std::int32_t>(PadSegment::kCenter));
}

TEST(UiPad, FastRotationReportsAllDetentsWithoutStationaryChanges)
{
    uai::ai::ui::PadSpec pad{};
    pad.id = 7U;
    pad.bounds = {0U, 0U, 32U, 32U};
    uai::ai::ui::PadPanel panel(&pad, 1U);

    panel.Update({true, 15U, 2U});
    const uai::ai::ui::Event clockwise = panel.Update({true, 29U, 15U});
    EXPECT_EQ(clockwise.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(clockwise.value, 2);
    EXPECT_EQ(panel.Update({true, 29U, 15U}).type, uai::ai::ui::EventType::kNone);
    const uai::ai::ui::Event counterclockwise = panel.Update({true, 15U, 2U});
    EXPECT_EQ(counterclockwise.type, uai::ai::ui::EventType::kChange);
    EXPECT_EQ(counterclockwise.value, -2);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);

    panel.Update({true, 6U, 6U});
    EXPECT_EQ(panel.Update({true, 25U, 6U}).value, 2);
    EXPECT_EQ(panel.Update({true, 6U, 6U}).value, -2);
    EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
}

TEST(UiPad, LeavingRingReanchorsWithoutChangingValue)
{
    uai::ai::ui::PadSpec pad{};
    pad.id = 7U;
    pad.bounds = {0U, 0U, 32U, 32U};
    const uai::ai::ui::TouchPoint interruptions[] = {
        {true, 16U, 16U},
        {true, 31U, 31U},
    };
    for (const uai::ai::ui::TouchPoint &interruption : interruptions) {
        uai::ai::ui::PadPanel panel(&pad, 1U);
        panel.Update({true, 15U, 2U});
        EXPECT_EQ(panel.Update(interruption).type, uai::ai::ui::EventType::kNone);
        EXPECT_EQ(panel.Update({true, 29U, 15U}).type, uai::ai::ui::EventType::kNone);
        const uai::ai::ui::Event resumed = panel.Update({true, 16U, 29U});
        EXPECT_EQ(resumed.type, uai::ai::ui::EventType::kChange);
        EXPECT_EQ(resumed.value, 2);
        EXPECT_EQ(panel.Update({false, 0U, 0U}).type, uai::ai::ui::EventType::kNone);
    }
}

TEST(UiPad, PaintColoursSegmentsAndCentre)
{
    uai::ai::ui::PadSpec pad{};
    pad.id = 7U;
    pad.bounds = {0U, 0U, 32U, 32U};
    pad.style.fill = 0x1111U;
    pad.style.pressed_fill = 0x2222U;
    pad.style.center_fill = 0x3333U;
    pad.style.border = 0x4444U;
    pad.style.arrow = 0x5555U;
    pad.style.border_width = 1U;
    uai::ai::ui::PadPanel panel(&pad, 1U);
    panel.Update({true, 29U, 16U});  /* hold RIGHT */

    TallFrame frame;
    uai::ai::ui::Canvas canvas(frame.Begin(), kWidth, 32U);
    panel.Paint(canvas);
    EXPECT_EQ(frame.At(16U, 16U), 0x3333U);  /* centre button */
    EXPECT_EQ(frame.At(29U, 16U), 0x2222U);  /* pressed quadrant */
    EXPECT_EQ(frame.At(2U, 16U), 0x1111U);   /* idle quadrant */
    EXPECT_EQ(frame.At(27U, 16U), 0x5555U);  /* right arrow */
    EXPECT_EQ(frame.At(16U, 0U), 0x4444U);   /* outer border */
    EXPECT_EQ(frame.At(16U, 5U), 0x5555U);   /* up arrow apex row */
    EXPECT_EQ(frame.At(0U, 0U), kBackground);
    EXPECT_TRUE(frame.GuardsIntact());
}

} // namespace
