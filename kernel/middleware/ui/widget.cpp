#include "middleware/ui/widget.hpp"

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
            if (buttons_[index].bounds.Contains(sample.x, sample.y)) {
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
        canvas.FillRect(button.bounds, fill);
        canvas.DrawFrame(button.bounds, button.style.border_width,
                         button.style.border);
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

Screen::Screen(const ScreenSpec &spec)
    : spec_(spec),
      buttons_(spec.buttons, spec.button_count),
      labels_(spec.labels, spec.label_count),
      sliders_(spec.sliders, spec.slider_count)
{
}

Event Screen::Update(const TouchPoint &sample)
{
    /* Both panels see every sample so their press tracking stays in sync;
     * only one of them can own a given touch-down position. */
    const Event button_event = buttons_.Update(sample);
    const Event slider_event = sliders_.Update(sample);
    return button_event.type != EventType::kNone ? button_event : slider_event;
}

void Screen::Paint(Canvas &canvas) const
{
    if (spec_.background == Background::kSolid) {
        canvas.FillRect({0U, 0U, canvas.Width(), canvas.Height()}, spec_.color);
    }
    buttons_.Paint(canvas);
    sliders_.Paint(canvas);
    labels_.Paint(canvas);
}

} // namespace uai::ai::ui
