#include "middleware/ui/widget.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace uai::ai::ui {

namespace {

/* Icons are drawn from filled rectangles so the host preview can mirror
 * them exactly; the icon square is half the smaller button dimension. */
void DrawIcon(Canvas &canvas, const Rect &bounds, Icon icon, std::uint16_t color)
{
    const std::uint16_t extent =
        (bounds.width < bounds.height ? bounds.width : bounds.height) / 2U;
    if (extent < 8U) {
        return;
    }
    const std::uint16_t x0 = static_cast<std::uint16_t>(bounds.x + (bounds.width - extent) / 2U);
    const std::uint16_t y0 = static_cast<std::uint16_t>(bounds.y + (bounds.height - extent) / 2U);
    const std::uint16_t bar = static_cast<std::uint16_t>(extent / 6U > 0U ? extent / 6U : 1U);
    switch (icon) {
    case Icon::kMenu:
        for (std::uint16_t row = 0U; row < 3U; ++row) {
            const std::uint16_t y = static_cast<std::uint16_t>(
                y0 + row * (extent - bar) / 2U);
            canvas.FillRect({x0, y, extent, bar}, color);
        }
        break;
    case Icon::kBack: {
        const std::uint16_t mid = static_cast<std::uint16_t>(y0 + extent / 2U);
        canvas.FillRect({x0, static_cast<std::uint16_t>(mid - bar / 2U), extent, bar}, color);
        for (std::uint16_t i = 0U; i < extent / 2U; ++i) {
            canvas.FillRect({static_cast<std::uint16_t>(x0 + i),
                             static_cast<std::uint16_t>(mid - i), bar, bar}, color);
            canvas.FillRect({static_cast<std::uint16_t>(x0 + i),
                             static_cast<std::uint16_t>(mid + i), bar, bar}, color);
        }
        break;
    }
    case Icon::kClose:
        for (std::uint16_t i = 0U; i + bar <= extent; ++i) {
            canvas.FillRect({static_cast<std::uint16_t>(x0 + i),
                             static_cast<std::uint16_t>(y0 + i), bar, bar}, color);
            canvas.FillRect({static_cast<std::uint16_t>(x0 + extent - bar - i),
                             static_cast<std::uint16_t>(y0 + i), bar, bar}, color);
        }
        break;
    case Icon::kNone:
        break;
    }
}

} // namespace

std::int32_t ButtonPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (buttons_[index].id == id) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

Event ButtonPanel::Update(const TouchPoint &sample)
{
    Event event{};
    const bool was_active = touch_active_;
    touch_active_ = sample.active;

    if (sample.active && !was_active) {
        for (std::size_t index = 0U; index < count_; ++index) {
            if (buttons_[index].Contains(sample.x, sample.y)) {
                pressed_index_ = static_cast<std::int32_t>(index);
                event.type = EventType::kPress;
                event.widget_id = buttons_[index].id;
                event.x = sample.x;
                event.y = sample.y;
                return event;
            }
        }
        return event;
    }

    if (!sample.active && was_active && pressed_index_ >= 0) {
        event.type = EventType::kTap;
        event.widget_id = buttons_[static_cast<std::size_t>(pressed_index_)].id;
        pressed_index_ = -1;
    }
    return event;
}

void ButtonPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const ButtonSpec &button = buttons_[index];
        const bool pressed = static_cast<std::int32_t>(index) == pressed_index_;
        const std::uint16_t fill = pressed ? button.style.pressed_fill
                                   : checked_[index] ? button.style.checked_fill
                                                     : button.style.fill;
        if (button.shape == Shape::kEllipse) {
            canvas.FillEllipse(button.bounds, fill);
            canvas.DrawEllipseFrame(button.bounds, button.style.border_width,
                                    button.style.border);
        } else {
            canvas.FillRect(button.bounds, fill);
            canvas.DrawFrame(button.bounds, button.style.border_width,
                             button.style.border);
        }
        if (button.icon != Icon::kNone) {
            DrawIcon(canvas, button.bounds, button.icon, button.style.text);
        } else {
            canvas.DrawTextCentered(button.bounds, button.label,
                                    button.style.text_scale, button.style.text);
        }
    }
}

bool ButtonPanel::IsPressed(std::uint16_t id) const
{
    return pressed_index_ >= 0 &&
           buttons_[static_cast<std::size_t>(pressed_index_)].id == id;
}

void ButtonPanel::SetChecked(std::uint16_t id, bool checked)
{
    const std::int32_t index = IndexOf(id);
    if (index >= 0) {
        checked_[static_cast<std::size_t>(index)] = checked;
    }
}

bool ButtonPanel::IsChecked(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index >= 0 && checked_[static_cast<std::size_t>(index)];
}

LabelPanel::LabelPanel(const LabelSpec *labels, std::size_t count)
    : labels_(labels),
      count_(labels != nullptr && count <= kMaxLabels ? count : 0U)
{
    for (std::size_t index = 0U; index < count_; ++index) {
        std::strncpy(text_[index], labels_[index].text, kLabelTextCapacity - 1U);
    }
}

std::int32_t LabelPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (labels_[index].id == id) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

bool LabelPanel::SetText(std::uint16_t id, const char *text)
{
    const std::int32_t index = IndexOf(id);
    if (index < 0 || text == nullptr) {
        return false;
    }
    char *slot = text_[static_cast<std::size_t>(index)];
    std::strncpy(slot, text, kLabelTextCapacity - 1U);
    slot[kLabelTextCapacity - 1U] = '\0';
    return true;
}

const char *LabelPanel::Text(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index < 0 ? "" : text_[static_cast<std::size_t>(index)];
}

void LabelPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const LabelSpec &label = labels_[index];
        const LabelStyle &style = label.style;
        if (style.has_fill) {
            canvas.FillRect(label.bounds, style.fill);
        }
        const char *text = text_[index];
        const std::uint32_t text_width = TextWidth(text, style.text_scale);
        const std::uint32_t text_height =
            static_cast<std::uint32_t>(kGlyphHeight) * style.text_scale;
        const std::uint32_t padding =
            2U * style.padding < label.bounds.width ? style.padding : 0U;
        const std::uint32_t inner_width = label.bounds.width - 2U * padding;
        std::uint32_t x = label.bounds.x + padding;
        if (text_width < inner_width) {
            if (style.align == TextAlign::kCenter) {
                x += (inner_width - text_width) / 2U;
            } else if (style.align == TextAlign::kRight) {
                x += inner_width - text_width;
            }
        }
        const std::uint32_t y = text_height < label.bounds.height
            ? label.bounds.y + (label.bounds.height - text_height) / 2U
            : label.bounds.y;
        canvas.DrawText(static_cast<std::uint16_t>(x),
                        static_cast<std::uint16_t>(y), text,
                        style.text_scale, style.text);
    }
}

void PainterGroup::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (painters_[index] != nullptr) {
            painters_[index]->Paint(canvas);
        }
    }
}

namespace {
constexpr std::uint16_t kTrackHeight = 8U;
constexpr std::uint16_t kKnobWidth = 16U;
constexpr std::uint16_t kKnobMargin = 6U;
} // namespace

SliderPanel::SliderPanel(const SliderSpec *sliders, std::size_t count)
    : sliders_(sliders),
      count_(sliders != nullptr && count <= kMaxSliders ? count : 0U)
{
    for (std::size_t index = 0U; index < count_; ++index) {
        values_[index] = Clamp(sliders_[index], sliders_[index].initial);
    }
}

std::int32_t SliderPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (sliders_[index].id == id) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

std::int32_t SliderPanel::Clamp(const SliderSpec &slider, std::int32_t value)
{
    if (value < slider.minimum) return slider.minimum;
    if (value > slider.maximum) return slider.maximum;
    return value;
}

Rect SliderPanel::TrackOf(const SliderSpec &slider)
{
    /* The caption row takes the top; the track sits in the lower half with
     * room for the knob to overhang on both ends. */
    const std::uint16_t caption =
        static_cast<std::uint16_t>(kGlyphHeight * slider.style.text_scale + 4U);
    const std::uint16_t inset = kKnobWidth / 2U;
    const std::uint16_t x = static_cast<std::uint16_t>(slider.bounds.x + inset);
    const std::uint16_t width = slider.bounds.width > 2U * inset
        ? static_cast<std::uint16_t>(slider.bounds.width - 2U * inset) : 1U;
    const std::uint16_t lower_top = static_cast<std::uint16_t>(slider.bounds.y + caption);
    const std::uint16_t lower_height = slider.bounds.height > caption
        ? static_cast<std::uint16_t>(slider.bounds.height - caption) : kTrackHeight;
    const std::uint16_t y = static_cast<std::uint16_t>(
        lower_top + (lower_height > kTrackHeight ? (lower_height - kTrackHeight) / 2U : 0U));
    return {x, y, width, kTrackHeight};
}

std::int32_t SliderPanel::ValueAt(const SliderSpec &slider, std::uint16_t x)
{
    const Rect track = TrackOf(slider);
    const std::int32_t span = slider.maximum - slider.minimum;
    if (span <= 0 || track.width <= 1U) {
        return slider.minimum;
    }
    std::int32_t offset = static_cast<std::int32_t>(x) - track.x;
    if (offset < 0) offset = 0;
    if (offset > track.width - 1) offset = track.width - 1;
    /* Round to the nearest step so the ends are reachable. */
    const std::int32_t raw = slider.minimum +
        (offset * span + (track.width - 1) / 2) / (track.width - 1);
    const std::int32_t step = slider.step > 0 ? slider.step : 1;
    const std::int32_t snapped = slider.minimum +
        ((raw - slider.minimum + step / 2) / step) * step;
    return Clamp(slider, snapped);
}

Event SliderPanel::Update(const TouchPoint &sample)
{
    Event event{};
    const bool was_active = touch_active_;
    touch_active_ = sample.active;

    if (sample.active && !was_active) {
        active_index_ = -1;
        for (std::size_t index = 0U; index < count_; ++index) {
            if (sliders_[index].bounds.Contains(sample.x, sample.y)) {
                active_index_ = static_cast<std::int32_t>(index);
                break;
            }
        }
    }
    if (!sample.active) {
        active_index_ = -1;
        return event;
    }
    if (active_index_ < 0) {
        return event;
    }
    const std::size_t index = static_cast<std::size_t>(active_index_);
    const std::int32_t value = ValueAt(sliders_[index], sample.x);
    if (value == values_[index]) {
        return event;
    }
    values_[index] = value;
    event.type = EventType::kChange;
    event.widget_id = sliders_[index].id;
    event.x = sample.x;
    event.y = sample.y;
    event.value = value;
    return event;
}

std::int32_t SliderPanel::Value(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index < 0 ? 0 : values_[static_cast<std::size_t>(index)];
}

bool SliderPanel::SetValue(std::uint16_t id, std::int32_t value)
{
    const std::int32_t index = IndexOf(id);
    if (index < 0) {
        return false;
    }
    values_[static_cast<std::size_t>(index)] =
        Clamp(sliders_[static_cast<std::size_t>(index)], value);
    return true;
}

void SliderPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const SliderSpec &slider = sliders_[index];
        const SliderStyle &style = slider.style;
        const Rect track = TrackOf(slider);
        const std::int32_t span = slider.maximum - slider.minimum;
        const std::int32_t position = span > 0
            ? (values_[index] - slider.minimum) * (track.width - 1) / span
            : 0;

        canvas.DrawText(slider.bounds.x, slider.bounds.y, slider.label,
                        style.text_scale, style.text);
        if (style.show_value) {
            char value_text[12];
            std::snprintf(value_text, sizeof(value_text), "%d",
                          static_cast<int>(values_[index]));
            const std::uint32_t width = TextWidth(value_text, style.text_scale);
            canvas.DrawText(static_cast<std::uint16_t>(
                                slider.bounds.x + slider.bounds.width - width),
                            slider.bounds.y, value_text, style.text_scale,
                            style.text);
        }
        canvas.FillRect(track, style.track);
        canvas.FillRect({track.x, track.y, static_cast<std::uint16_t>(position + 1),
                         track.height}, style.fill);
        const std::uint16_t knob_height =
            static_cast<std::uint16_t>(track.height + 2U * kKnobMargin);
        canvas.FillRect({static_cast<std::uint16_t>(track.x + position - kKnobWidth / 2U),
                         static_cast<std::uint16_t>(track.y - kKnobMargin),
                         kKnobWidth, knob_height}, style.knob);
    }
}

/* --- Dial ---------------------------------------------------------------- */

namespace {
constexpr std::int32_t kDialSweepDegrees = 270;
constexpr std::int32_t kDialStartDegrees = 135;  /* bottom-left, clockwise */

/* Clockwise degrees (0 = +x) of a doubled-coordinate offset. */
std::int32_t DegreesOf(std::int32_t dx2, std::int32_t dy2)
{
    const float radians = std::atan2(static_cast<float>(dy2), static_cast<float>(dx2));
    std::int32_t degrees = static_cast<std::int32_t>(
        radians * (180.0F / 3.14159265F) + (radians >= 0.0F ? 0.5F : -0.5F));
    if (degrees < 0) degrees += 360;
    return degrees % 360;
}

/* Sweep from the arc start, or -1 inside the bottom gap. */
std::int32_t SweepOfOffset(std::int32_t dx2, std::int32_t dy2)
{
    const std::int32_t sweep = (DegreesOf(dx2, dy2) - kDialStartDegrees + 360) % 360;
    return sweep <= kDialSweepDegrees ? sweep : -1;
}
} // namespace

DialPanel::DialPanel(const DialSpec *dials, std::size_t count)
    : dials_(dials), count_(dials != nullptr && count <= kMaxDials ? count : 0U)
{
    for (std::size_t index = 0U; index < count_; ++index) {
        values_[index] = Clamp(dials_[index], dials_[index].initial);
    }
}

std::int32_t DialPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (dials_[index].id == id) return static_cast<std::int32_t>(index);
    }
    return -1;
}

std::int32_t DialPanel::Clamp(const DialSpec &dial, std::int32_t value)
{
    if (value < dial.minimum) return dial.minimum;
    if (value > dial.maximum) return dial.maximum;
    return value;
}

Rect DialPanel::DiscOf(const DialSpec &dial)
{
    const std::uint16_t caption =
        static_cast<std::uint16_t>(kGlyphHeight * dial.style.text_scale + 4U);
    const std::uint16_t free_height =
        dial.bounds.height > caption ? static_cast<std::uint16_t>(dial.bounds.height - caption) : 1U;
    const std::uint16_t side = dial.bounds.width < free_height ? dial.bounds.width : free_height;
    return {static_cast<std::uint16_t>(dial.bounds.x + (dial.bounds.width - side) / 2U),
            static_cast<std::uint16_t>(dial.bounds.y + caption + (free_height - side) / 2U),
            side, side};
}

std::uint16_t DialPanel::SweepOf(const DialSpec &dial, std::int32_t value)
{
    const std::int32_t span = dial.maximum - dial.minimum;
    if (span <= 0) return 0U;
    return static_cast<std::uint16_t>(
        (Clamp(dial, value) - dial.minimum) * kDialSweepDegrees / span);
}

std::int32_t DialPanel::ValueAt(const DialSpec &dial, std::uint16_t x, std::uint16_t y)
{
    const Rect disc = DiscOf(dial);
    const std::int32_t dx2 = 2 * (static_cast<std::int32_t>(x) - disc.x) + 1 - disc.width;
    const std::int32_t dy2 = 2 * (static_cast<std::int32_t>(y) - disc.y) + 1 - disc.height;
    std::int32_t sweep = SweepOfOffset(dx2, dy2);
    if (sweep < 0) {
        /* Bottom gap: snap to whichever end is nearer. */
        sweep = dx2 < 0 ? 0 : kDialSweepDegrees;
    }
    const std::int32_t span = dial.maximum - dial.minimum;
    const std::int32_t step = dial.step > 0 ? dial.step : 1;
    const std::int32_t raw = dial.minimum +
        (sweep * span + kDialSweepDegrees / 2) / kDialSweepDegrees;
    const std::int32_t snapped = dial.minimum +
        ((raw - dial.minimum + step / 2) / step) * step;
    return Clamp(dial, snapped);
}

Event DialPanel::Update(const TouchPoint &sample)
{
    Event event{};
    const bool was_active = touch_active_;
    touch_active_ = sample.active;
    if (sample.active && !was_active) {
        active_index_ = -1;
        for (std::size_t index = 0U; index < count_; ++index) {
            if (InsideEllipse(DiscOf(dials_[index]), sample.x, sample.y)) {
                active_index_ = static_cast<std::int32_t>(index);
                break;
            }
        }
    }
    if (!sample.active) {
        active_index_ = -1;
        return event;
    }
    if (active_index_ < 0) return event;
    const std::size_t index = static_cast<std::size_t>(active_index_);
    const std::int32_t value = ValueAt(dials_[index], sample.x, sample.y);
    if (value == values_[index]) return event;
    values_[index] = value;
    event.type = EventType::kChange;
    event.widget_id = dials_[index].id;
    event.x = sample.x;
    event.y = sample.y;
    event.value = value;
    return event;
}

std::int32_t DialPanel::Value(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index < 0 ? 0 : values_[static_cast<std::size_t>(index)];
}

bool DialPanel::SetValue(std::uint16_t id, std::int32_t value)
{
    const std::int32_t index = IndexOf(id);
    if (index < 0) return false;
    values_[static_cast<std::size_t>(index)] =
        Clamp(dials_[static_cast<std::size_t>(index)], value);
    return true;
}

void DialPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const DialSpec &dial = dials_[index];
        const DialStyle &style = dial.style;
        const Rect disc = DiscOf(dial);
        const std::int32_t outer = disc.width;  /* doubled radius */
        const std::int32_t thickness = disc.width / 8U > 4U ? disc.width / 8U : 4U;
        const std::int32_t inner = outer - 2 * thickness;
        const std::int32_t current = SweepOf(dial, values_[index]);

        canvas.DrawText(dial.bounds.x, dial.bounds.y, dial.label, style.text_scale, style.text);
        if (style.show_value) {
            char value_text[12];
            std::snprintf(value_text, sizeof(value_text), "%d", static_cast<int>(values_[index]));
            canvas.DrawText(static_cast<std::uint16_t>(
                                dial.bounds.x + dial.bounds.width - TextWidth(value_text, style.text_scale)),
                            dial.bounds.y, value_text, style.text_scale, style.text);
        }
        for (std::uint32_t y = disc.y; y < static_cast<std::uint32_t>(disc.y) + disc.height; ++y) {
            for (std::uint32_t x = disc.x; x < static_cast<std::uint32_t>(disc.x) + disc.width; ++x) {
                const std::int32_t dx2 = 2 * (static_cast<std::int32_t>(x) - disc.x) + 1 - disc.width;
                const std::int32_t dy2 = 2 * (static_cast<std::int32_t>(y) - disc.y) + 1 - disc.height;
                const std::int32_t d2 = dx2 * dx2 + dy2 * dy2;
                if (d2 > outer * outer) continue;
                if (d2 <= inner * inner) {
                    canvas.PutPixel(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), style.face);
                    continue;
                }
                const std::int32_t sweep = SweepOfOffset(dx2, dy2);
                if (sweep < 0) continue;  /* bottom gap stays open */
                canvas.PutPixel(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                sweep <= current ? style.fill : style.track);
            }
        }
        /* Pointer: a small disc on the ring centre line at the current angle. */
        const float angle = static_cast<float>(kDialStartDegrees + current) * (3.14159265F / 180.0F);
        const float radius = static_cast<float>(outer - thickness) / 2.0F;
        const float cx = static_cast<float>(disc.x) + static_cast<float>(disc.width) / 2.0F;
        const float cy = static_cast<float>(disc.y) + static_cast<float>(disc.height) / 2.0F;
        const std::int32_t pointer = thickness / 2 + 2;
        const std::int32_t px = static_cast<std::int32_t>(cx + radius * std::cos(angle));
        const std::int32_t py = static_cast<std::int32_t>(cy + radius * std::sin(angle));
        canvas.FillEllipse({static_cast<std::uint16_t>(px - pointer),
                            static_cast<std::uint16_t>(py - pointer),
                            static_cast<std::uint16_t>(2 * pointer),
                            static_cast<std::uint16_t>(2 * pointer)}, style.pointer);
    }
}

/* --- Wheel --------------------------------------------------------------- */

WheelPanel::WheelPanel(const WheelSpec *wheels, std::size_t count)
    : wheels_(wheels), count_(wheels != nullptr && count <= kMaxWheels ? count : 0U)
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const WheelSpec &wheel = wheels_[index];
        selected_[index] = wheel.item_count == 0U ? 0
            : static_cast<std::int32_t>(wheel.initial < wheel.item_count ? wheel.initial : 0U);
    }
}

std::int32_t WheelPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (wheels_[index].id == id) return static_cast<std::int32_t>(index);
    }
    return -1;
}

std::uint16_t WheelPanel::RowHeightOf(const WheelSpec &wheel)
{
    return static_cast<std::uint16_t>(kGlyphHeight * wheel.style.text_scale + 8U);
}

Event WheelPanel::Update(const TouchPoint &sample)
{
    Event event{};
    const bool was_active = touch_active_;
    touch_active_ = sample.active;
    if (sample.active && !was_active) {
        active_index_ = -1;
        for (std::size_t index = 0U; index < count_; ++index) {
            if (wheels_[index].bounds.Contains(sample.x, sample.y) &&
                wheels_[index].item_count > 0U) {
                active_index_ = static_cast<std::int32_t>(index);
                anchor_selected_ = selected_[index];
                anchor_y_ = sample.y;
                break;
            }
        }
    }
    if (!sample.active) {
        active_index_ = -1;
        return event;
    }
    if (active_index_ < 0) return event;
    const std::size_t index = static_cast<std::size_t>(active_index_);
    const WheelSpec &wheel = wheels_[index];
    const std::int32_t row = RowHeightOf(wheel);
    /* Dragging up brings the lower items into the band: +1 per row. */
    const std::int32_t travel = static_cast<std::int32_t>(anchor_y_) - sample.y;
    const std::int32_t steps = travel >= 0 ? (travel + row / 2) / row : -((-travel + row / 2) / row);
    std::int32_t selected = anchor_selected_ + steps;
    const std::int32_t last = static_cast<std::int32_t>(wheel.item_count) - 1;
    if (selected < 0) selected = 0;
    if (selected > last) selected = last;
    if (selected == selected_[index]) return event;
    selected_[index] = selected;
    event.type = EventType::kChange;
    event.widget_id = wheel.id;
    event.x = sample.x;
    event.y = sample.y;
    event.value = selected;
    return event;
}

std::int32_t WheelPanel::Value(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index < 0 ? 0 : selected_[static_cast<std::size_t>(index)];
}

bool WheelPanel::SetValue(std::uint16_t id, std::int32_t selected)
{
    const std::int32_t index = IndexOf(id);
    if (index < 0) return false;
    const WheelSpec &wheel = wheels_[static_cast<std::size_t>(index)];
    if (selected < 0 || static_cast<std::size_t>(selected) >= wheel.item_count) return false;
    selected_[static_cast<std::size_t>(index)] = selected;
    return true;
}

const char *WheelPanel::ItemText(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    if (index < 0) return "";
    const WheelSpec &wheel = wheels_[static_cast<std::size_t>(index)];
    const std::int32_t selected = selected_[static_cast<std::size_t>(index)];
    return wheel.item_count == 0U ? "" : wheel.items[selected];
}

void WheelPanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const WheelSpec &wheel = wheels_[index];
        const WheelStyle &style = wheel.style;
        const Rect &bounds = wheel.bounds;
        const std::int32_t row = RowHeightOf(wheel);
        const std::int32_t band_y = bounds.y + (static_cast<std::int32_t>(bounds.height) - row) / 2;
        canvas.FillRect(bounds, style.fill);
        canvas.FillRect({bounds.x, static_cast<std::uint16_t>(band_y), bounds.width,
                         static_cast<std::uint16_t>(row)}, style.highlight);
        canvas.DrawFrame(bounds, 1U, style.border);
        const std::int32_t selected = selected_[index];
        const std::int32_t reach = (static_cast<std::int32_t>(bounds.height) / row) / 2 + 1;
        for (std::int32_t offset = -reach; offset <= reach; ++offset) {
            const std::int32_t item = selected + offset;
            if (item < 0 || static_cast<std::size_t>(item) >= wheel.item_count) continue;
            const std::int32_t top = band_y + offset * row;
            if (top < bounds.y || top + row > bounds.y + static_cast<std::int32_t>(bounds.height)) continue;
            canvas.DrawTextCentered({bounds.x, static_cast<std::uint16_t>(top), bounds.width,
                                     static_cast<std::uint16_t>(row)},
                                    wheel.items[item], style.text_scale,
                                    offset == 0 ? style.selected_text : style.text);
        }
    }
}

/* --- Number -------------------------------------------------------------- */

NumberPanel::NumberPanel(const NumberSpec *numbers, std::size_t count)
    : numbers_(numbers), count_(numbers != nullptr && count <= kMaxNumbers ? count : 0U)
{
    for (std::size_t index = 0U; index < count_; ++index) {
        values_[index] = numbers_[index].initial;
    }
}

std::int32_t NumberPanel::IndexOf(std::uint16_t id) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        if (numbers_[index].id == id) return static_cast<std::int32_t>(index);
    }
    return -1;
}

bool NumberPanel::SetValue(std::uint16_t id, std::int32_t value)
{
    const std::int32_t index = IndexOf(id);
    if (index < 0) return false;
    values_[static_cast<std::size_t>(index)] = value;
    return true;
}

std::int32_t NumberPanel::Value(std::uint16_t id) const
{
    const std::int32_t index = IndexOf(id);
    return index < 0 ? 0 : values_[static_cast<std::size_t>(index)];
}

void NumberPanel::Format(const NumberSpec &number, std::int32_t value,
                         char (&out)[kNumberTextCapacity])
{
    std::int32_t divisor = 1;
    for (std::uint8_t i = 0U; i < number.decimals && i < 6U; ++i) divisor *= 10;
    const std::uint32_t magnitude = value < 0
        ? 0U - static_cast<std::uint32_t>(value) : static_cast<std::uint32_t>(value);
    const std::uint32_t whole = magnitude / static_cast<std::uint32_t>(divisor);
    const std::uint32_t fraction = magnitude % static_cast<std::uint32_t>(divisor);
    if (number.decimals == 0U) {
        std::snprintf(out, sizeof(out), "%s%u%s", value < 0 ? "-" : "",
                      static_cast<unsigned int>(whole), number.unit);
    } else {
        std::snprintf(out, sizeof(out), "%s%u.%0*u%s", value < 0 ? "-" : "",
                      static_cast<unsigned int>(whole),
                      static_cast<int>(number.decimals < 6U ? number.decimals : 6U),
                      static_cast<unsigned int>(fraction), number.unit);
    }
}

void NumberPanel::Paint(Canvas &canvas) const
{
    constexpr std::uint16_t kPadding = 4U;
    for (std::size_t index = 0U; index < count_; ++index) {
        const NumberSpec &number = numbers_[index];
        const NumberStyle &style = number.style;
        const Rect &bounds = number.bounds;
        if (style.has_fill) canvas.FillRect(bounds, style.fill);

        const std::uint8_t caption_scale =
            static_cast<std::uint8_t>(style.text_scale / 2U > 0U ? style.text_scale / 2U : 1U);
        std::uint16_t caption = 0U;
        if (number.label[0] != '\0') {
            canvas.DrawText(static_cast<std::uint16_t>(bounds.x + kPadding),
                            static_cast<std::uint16_t>(bounds.y + kPadding),
                            number.label, caption_scale, style.text);
            caption = static_cast<std::uint16_t>(kGlyphHeight * caption_scale + 2U * kPadding);
        }
        char text[kNumberTextCapacity];
        Format(number, values_[index], text);
        const std::uint32_t text_width = TextWidth(text, style.text_scale);
        const std::uint32_t text_height = static_cast<std::uint32_t>(kGlyphHeight) * style.text_scale;
        const std::uint32_t inner = bounds.width > 2U * kPadding ? bounds.width - 2U * kPadding : 0U;
        std::uint32_t x = bounds.x + kPadding;
        if (text_width < inner) {
            if (style.align == TextAlign::kCenter) x += (inner - text_width) / 2U;
            else if (style.align == TextAlign::kRight) x += inner - text_width;
        }
        const std::uint32_t area_top = bounds.y + caption;
        const std::uint32_t area_height = bounds.height > caption ? bounds.height - caption : 0U;
        const std::uint32_t y = text_height < area_height
            ? area_top + (area_height - text_height) / 2U : area_top;
        canvas.DrawText(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), text,
                        style.text_scale, style.text);
    }
}

/* --- Image --------------------------------------------------------------- */

void ImagePanel::Paint(Canvas &canvas) const
{
    for (std::size_t index = 0U; index < count_; ++index) {
        const ImageSpec &image = images_[index];
        canvas.Blit(image.bounds.x, image.bounds.y, image.pixels, image.bounds.width,
                    image.bounds.height, image.has_transparent, image.transparent);
    }
}

/* --- Screen -------------------------------------------------------------- */

Screen::Screen(const ScreenSpec &spec)
    : spec_(spec),
      buttons_(spec.buttons, spec.button_count),
      labels_(spec.labels, spec.label_count),
      sliders_(spec.sliders, spec.slider_count),
      dials_(spec.dials, spec.dial_count),
      wheels_(spec.wheels, spec.wheel_count),
      numbers_(spec.numbers, spec.number_count),
      images_(spec.images, spec.image_count)
{
}

Event Screen::Update(const TouchPoint &sample)
{
    /* Every panel sees every sample so their press tracking stays in sync;
     * only one of them can own a given touch-down position. */
    const Event button_event = buttons_.Update(sample);
    const Event slider_event = sliders_.Update(sample);
    const Event dial_event = dials_.Update(sample);
    const Event wheel_event = wheels_.Update(sample);
    if (button_event.type != EventType::kNone) return button_event;
    if (slider_event.type != EventType::kNone) return slider_event;
    if (dial_event.type != EventType::kNone) return dial_event;
    return wheel_event;
}

void Screen::Paint(Canvas &canvas) const
{
    if (spec_.background == Background::kSolid) {
        canvas.FillRect({0U, 0U, canvas.Width(), canvas.Height()}, spec_.color);
    }
    images_.Paint(canvas);
    buttons_.Paint(canvas);
    sliders_.Paint(canvas);
    dials_.Paint(canvas);
    wheels_.Paint(canvas);
    numbers_.Paint(canvas);
    labels_.Paint(canvas);
}

} // namespace uai::ai::ui
